#include "rg_system.h"
#include "rg_input.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include <driver/gpio.h>
#include <driver/adc.h>
// This is a lazy way to silence deprecation notices on some esp-idf versions...
// This hardcoded value is the first thing to check if something stops working!
#define ADC_ATTEN_DB_11 3
#else
#include <SDL2/SDL.h>
#endif

#if RG_BATTERY_DRIVER == 1
#include <esp_adc_cal.h>
static esp_adc_cal_characteristics_t adc_chars;
#endif

#ifdef RG_GAMEPAD_ADC_MAP
static rg_keymap_adc_t keymap_adc[] = RG_GAMEPAD_ADC_MAP;
#endif
#ifdef RG_GAMEPAD_GPIO_MAP
static rg_keymap_gpio_t keymap_gpio[] = RG_GAMEPAD_GPIO_MAP;
#endif
#ifdef RG_GAMEPAD_I2C_MAP
static rg_keymap_i2c_t keymap_i2c[] = RG_GAMEPAD_I2C_MAP;
#endif
#ifdef RG_GAMEPAD_KBD_MAP
static rg_keymap_kbd_t keymap_kbd[] = RG_GAMEPAD_KBD_MAP;
#endif
#ifdef RG_GAMEPAD_SERIAL_MAP
static rg_keymap_serial_t keymap_serial[] = RG_GAMEPAD_SERIAL_MAP;
#endif
#ifdef RG_GAMEPAD_VIRT_MAP
static rg_keymap_virt_t keymap_virt[] = RG_GAMEPAD_VIRT_MAP;
#endif

static uint32_t gamepad_mapped = 0;

#if defined(RG_GAMEPAD_USE_ESPNOW) && defined(ESP_PLATFORM)
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#ifndef RG_GAMEPAD_WIFI_CHANNEL
#define RG_GAMEPAD_WIFI_CHANNEL 1
#endif

#ifndef RG_GAMEPAD_SYSTEM_ID
#define RG_GAMEPAD_SYSTEM_ID 0
#endif

#define RG_ESPNOW_GAMEPAD_MAGIC 0x4752 // 'R', 'G'

#define RG_ESPNOW_CMD_ACK          0xFFFF // Normal state ACK / PONG
#define RG_ESPNOW_CMD_PAIR_REQ     0xFFFE // Gamepad -> Console: Request pairing
#define RG_ESPNOW_CMD_PAIR_ACK     0xFFFD // Console -> Gamepad: Confirm pairing with console MAC
#define RG_ESPNOW_CMD_CHAN_SWITCH  0xFFFC // Console -> Gamepad: Switch channel immediately

typedef struct __attribute__((packed)) {
    uint16_t magic;          // 0x4752 ('R', 'G')
    uint16_t system_id;      // System/Console ID (0 = all, >0 = isolated room/console)
    uint16_t buttons;        // Bitmask RG_KEY_* or RG_ESPNOW_CMD_*
    uint8_t  seq;            // Packet sequence number (0-255)
    uint8_t  player_id;      // 0 = Player 1, 1 = Player 2
    uint8_t  console_mac[6]; // Console STA MAC
    uint8_t  channel;        // Primary channel hint
    uint8_t  reserved;       // Alignment padding
} rg_espnow_gamepad_packet_t;

static uint32_t espnow_gamepad_state = 0;
static int64_t espnow_last_packet_time = 0;
static bool espnow_gamepad_connected = false;
static uint8_t espnow_gamepad_mac[6];
static bool espnow_gamepad_bound = false;
static uint8_t espnow_bonded_mac[6] = {0};
static bool espnow_has_bonded_mac = false;
static uint8_t console_self_mac[6] = {0};
static uint8_t espnow_current_channel = RG_GAMEPAD_WIFI_CHANNEL;
static const uint8_t espnow_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// Protects espnow_gamepad_state written from WiFi task, read from emulator task (dual-core)
static portMUX_TYPE espnow_mux = portMUX_INITIALIZER_UNLOCKED;

// Deferred worker: recv_cb only enqueues here; actual NVS writes and esp_now_send happen in worker task
typedef enum { ESPNOW_EV_ACK, ESPNOW_EV_PAIR_ACK, ESPNOW_EV_NVS_SAVE } espnow_ev_type_t;
typedef struct {
    espnow_ev_type_t type;
    rg_espnow_gamepad_packet_t pkt;   // packet to send (ACK / PAIR_ACK)
    uint8_t mac[6];
} espnow_ev_t;
static QueueHandle_t espnow_ev_queue;

static void espnow_worker_task(void *arg)
{
    espnow_ev_t ev;
    nvs_handle_t h;
    while (true)
    {
        if (xQueueReceive(espnow_ev_queue, &ev, portMAX_DELAY) != pdTRUE)
            continue;
        switch (ev.type)
        {
        case ESPNOW_EV_ACK:
        case ESPNOW_EV_PAIR_ACK:
            esp_now_send(espnow_broadcast_mac, (const uint8_t *)&ev.pkt, sizeof(ev.pkt));
            break;
        case ESPNOW_EV_NVS_SAVE:
            if (nvs_open("retro-go", NVS_READWRITE, &h) == ESP_OK)
            {
                nvs_set_blob(h, "gp_mac", ev.mac, 6);
                nvs_commit(h);
                nvs_close(h);
            }
            break;
        }
    }
}

static void espnow_enqueue_send(espnow_ev_type_t type, const rg_espnow_gamepad_packet_t *pkt)
{
    espnow_ev_t ev = { .type = type };
    ev.pkt = *pkt;
    xQueueSend(espnow_ev_queue, &ev, 0); // non-blocking; drop if queue full
}

static void espnow_enqueue_nvs_save(const uint8_t *mac)
{
    espnow_ev_t ev = { .type = ESPNOW_EV_NVS_SAVE };
    memcpy(ev.mac, mac, 6);
    xQueueSend(espnow_ev_queue, &ev, 0);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    const uint8_t *src_mac = esp_now_info->src_addr;
#else
static void espnow_recv_cb(const uint8_t *src_mac, const uint8_t *data, int data_len)
{
#endif
    if (data_len < (int)sizeof(rg_espnow_gamepad_packet_t))
        return;

    const rg_espnow_gamepad_packet_t *packet = (const rg_espnow_gamepad_packet_t *)data;
    if (packet->magic != RG_ESPNOW_GAMEPAD_MAGIC)
        return;

    if (packet->buttons == RG_ESPNOW_CMD_ACK ||
        packet->buttons == RG_ESPNOW_CMD_PAIR_ACK ||
        packet->buttons == RG_ESPNOW_CMD_CHAN_SWITCH)
        return;

#if RG_GAMEPAD_SYSTEM_ID > 0
    if (packet->system_id != RG_GAMEPAD_SYSTEM_ID)
        return;
#endif

    int64_t now = rg_system_timer();

    // 1. Explicit pairing request
    if (packet->buttons == RG_ESPNOW_CMD_PAIR_REQ)
    {
        if (espnow_has_bonded_mac && espnow_gamepad_connected &&
            (now - espnow_last_packet_time < 2000000) &&
            memcmp(espnow_bonded_mac, src_mac, 6) != 0)
            return;

        memcpy(espnow_gamepad_mac, src_mac, 6);
        memcpy(espnow_bonded_mac, src_mac, 6);
        espnow_has_bonded_mac = true;
        espnow_gamepad_bound = true;
        // Defer NVS write to worker — never block in recv callback
        espnow_enqueue_nvs_save(src_mac);

        RG_LOGI("ESP-NOW: Pairing accepted from %02X:%02X:%02X:%02X:%02X:%02X",
                src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);

        rg_espnow_gamepad_packet_t pair_ack = {
            .magic = RG_ESPNOW_GAMEPAD_MAGIC,
            .system_id = RG_GAMEPAD_SYSTEM_ID,
            .buttons = RG_ESPNOW_CMD_PAIR_ACK,
            .seq = packet->seq,
            .player_id = 0,
            .channel = espnow_current_channel,
            .reserved = 0,
        };
        memcpy(pair_ack.console_mac, console_self_mac, 6);
        // Defer send to worker — esp_now_send must not be called from recv callback
        espnow_enqueue_send(ESPNOW_EV_PAIR_ACK, &pair_ack);
        return;
    }

    // 2. Normal button packets: bonding check
    if (espnow_has_bonded_mac)
    {
        if (memcmp(espnow_bonded_mac, src_mac, 6) != 0)
            return;
    }
    else
    {
        memcpy(espnow_gamepad_mac, src_mac, 6);
        memcpy(espnow_bonded_mac, src_mac, 6);
        espnow_has_bonded_mac = true;
        espnow_gamepad_bound = true;
        espnow_enqueue_nvs_save(src_mac);
        RG_LOGI("ESP-NOW: Auto-bonded to MAC %02X:%02X:%02X:%02X:%02X:%02X",
                src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
    }

    uint8_t active_radio_channel = espnow_current_channel;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&active_radio_channel, &second) == ESP_OK && active_radio_channel > 0)
        espnow_current_channel = active_radio_channel;

    // Defer ACK send to worker
    rg_espnow_gamepad_packet_t ack = {
        .magic = RG_ESPNOW_GAMEPAD_MAGIC,
        .system_id = packet->system_id,
        .buttons = RG_ESPNOW_CMD_ACK,
        .seq = packet->seq,
        .player_id = 0,
        .channel = espnow_current_channel,
        .reserved = 0,
    };
    memcpy(ack.console_mac, console_self_mac, 6);
    espnow_enqueue_send(ESPNOW_EV_ACK, &ack);

    portENTER_CRITICAL(&espnow_mux);
    espnow_gamepad_state = (uint32_t)packet->buttons;
    espnow_last_packet_time = now;
    portEXIT_CRITICAL(&espnow_mux);

    if (!espnow_gamepad_connected)
    {
        espnow_gamepad_connected = true;
        RG_LOGI("ESP-NOW: Wireless gamepad connected.");
    }
}

static void espnow_gamepad_init(void)
{
    // Load bonded MAC from NVS (check "gp_mac", fallback to legacy "gp_mac_p1")
    nvs_handle_t h;
    if (nvs_open("retro-go", NVS_READONLY, &h) == ESP_OK)
    {
        size_t len = 6;
        if ((nvs_get_blob(h, "gp_mac", espnow_bonded_mac, &len) == ESP_OK && len == 6) ||
            (nvs_get_blob(h, "gp_mac_p1", espnow_bonded_mac, &len) == ESP_OK && len == 6))
        {
            espnow_has_bonded_mac = true;
            memcpy(espnow_gamepad_mac, espnow_bonded_mac, 6);
            espnow_gamepad_bound = true;
            RG_LOGI("ESP-NOW: Loaded bonded gamepad MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                    espnow_bonded_mac[0], espnow_bonded_mac[1], espnow_bonded_mac[2],
                    espnow_bonded_mac[3], espnow_bonded_mac[4], espnow_bonded_mac[5]);
        }
        nvs_close(h);
    }

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK)
    {
        esp_netif_init();
        esp_event_loop_create_default();
        if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))
            esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_channel(RG_GAMEPAD_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);
    }
    else
    {
        esp_wifi_set_channel(RG_GAMEPAD_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    }

    esp_wifi_get_mac(WIFI_IF_STA, console_self_mac);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);
    esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_24M);

    // Worker task: handles deferred NVS writes and esp_now_send from recv callback
    espnow_ev_queue = xQueueCreate(16, sizeof(espnow_ev_t));
    xTaskCreate(espnow_worker_task, "espnow_wk", 3072, NULL, 5, NULL);

    if (esp_now_init() == ESP_OK)
    {
        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, espnow_broadcast_mac, 6);
        peer_info.channel = 0; // Follow current WiFi channel
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = false;
        esp_now_add_peer(&peer_info);

        esp_now_register_recv_cb(espnow_recv_cb);
        RG_LOGI("ESP-NOW wireless gamepad receiver ready (MAC: %02X:%02X:%02X:%02X:%02X:%02X).",
                console_self_mac[0], console_self_mac[1], console_self_mac[2],
                console_self_mac[3], console_self_mac[4], console_self_mac[5]);
    }
    else
    {
        RG_LOGW("Failed to initialize ESP-NOW.");
    }

    gamepad_mapped |= (RG_KEY_UP | RG_KEY_DOWN | RG_KEY_LEFT | RG_KEY_RIGHT |
                       RG_KEY_A | RG_KEY_B | RG_KEY_X | RG_KEY_Y |
                       RG_KEY_SELECT | RG_KEY_START | RG_KEY_MENU | RG_KEY_OPTION |
                       RG_KEY_L | RG_KEY_R);
}

typedef struct {
    uint8_t old_chan;
    uint8_t new_chan;
    rg_espnow_gamepad_packet_t pkt;
} espnow_chan_switch_args_t;

static void espnow_chan_switch_task(void *arg)
{
    espnow_chan_switch_args_t *a = (espnow_chan_switch_args_t *)arg;

    uint8_t current_radio_chan = 0;
    wifi_second_chan_t second_chan;
    esp_wifi_get_channel(&current_radio_chan, &second_chan);

    // 1. Transmit on old_chan so gamepad receives notice immediately.
    // Skip promiscuous hop if WiFi is actively connected — disrupts DHCP/data path.
    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifi_mode);
    bool wifi_connected = (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_APSTA);
    if (!wifi_connected && a->old_chan >= 1 && a->old_chan <= 13 && current_radio_chan != a->old_chan)
    {
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_channel(a->old_chan, WIFI_SECOND_CHAN_NONE);
        for (int i = 0; i < 8; i++)
        {
            esp_now_send(espnow_broadcast_mac, (const uint8_t *)&a->pkt, sizeof(a->pkt));
            vTaskDelay(1);
        }
        esp_wifi_set_channel(current_radio_chan, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);
    }

    // 2. Also transmit on new_chan
    for (int i = 0; i < 8; i++)
    {
        esp_now_send(espnow_broadcast_mac, (const uint8_t *)&a->pkt, sizeof(a->pkt));
        vTaskDelay(1);
    }

    RG_LOGI("ESP-NOW: Broadcasted channel switch notice (%d -> %d).", a->old_chan, a->new_chan);
    free(a);
    vTaskDelete(NULL);
}

void rg_input_espnow_notify_channel_switch(uint8_t new_channel)
{
    if (new_channel < 1 || new_channel > 13)
        return;

    uint8_t old_chan = espnow_current_channel;
    espnow_current_channel = new_channel;

    if (old_chan == new_channel)
        return;

    espnow_chan_switch_args_t *args = malloc(sizeof(espnow_chan_switch_args_t));
    if (!args)
    {
        RG_LOGW("ESP-NOW: channel switch notify OOM.");
        return;
    }

    args->old_chan = old_chan;
    args->new_chan = new_channel;
    args->pkt = (rg_espnow_gamepad_packet_t){
        .magic = RG_ESPNOW_GAMEPAD_MAGIC,
        .system_id = RG_GAMEPAD_SYSTEM_ID,
        .buttons = RG_ESPNOW_CMD_CHAN_SWITCH,
        .seq = 0,
        .player_id = 0,
        .channel = new_channel,
        .reserved = 0,
    };
    memcpy(args->pkt.console_mac, console_self_mac, 6);

    // Run in background task to avoid blocking the event loop with busy-wait sends
    if (xTaskCreate(espnow_chan_switch_task, "espnow_ch", 2048, args, 5, NULL) != pdPASS)
    {
        RG_LOGW("ESP-NOW: Failed to create channel switch task.");
        free(args);
    }
}
#endif
static bool input_task_running = false;
static uint32_t gamepad_state = -1; // _Atomic
static rg_battery_t battery_state = {0};

#define UPDATE_GLOBAL_MAP(keymap)                 \
    for (size_t i = 0; i < RG_COUNT(keymap); ++i) \
        gamepad_mapped |= keymap[i].key;          \

#ifdef ESP_PLATFORM
static inline int adc_get_raw(adc_unit_t unit, adc_channel_t channel)
{
    if (unit == ADC_UNIT_1)
    {
        return adc1_get_raw(channel);
    }
    else if (unit == ADC_UNIT_2)
    {
        int adc_raw_value = -1;
        if (adc2_get_raw(channel, ADC_WIDTH_MAX - 1, &adc_raw_value) != ESP_OK)
            RG_LOGE("ADC2 reading failed, this can happen while wifi is active.");
        return adc_raw_value;
    }
    RG_LOGE("Invalid ADC unit %d", (int)unit);
    return -1;
}
#endif

bool rg_input_read_battery_raw(rg_battery_t *out)
{
    uint32_t raw_value = 0;
    bool present = true;
    bool charging = false;

#if RG_BATTERY_DRIVER == 1 /* ADC */
    for (int i = 0; i < 4; ++i)
    {
        int value = adc_get_raw(RG_BATTERY_ADC_UNIT, RG_BATTERY_ADC_CHANNEL);
        if (value < 0)
            return false;
        raw_value += esp_adc_cal_raw_to_voltage(value, &adc_chars);
    }
    raw_value /= 4;
#elif RG_BATTERY_DRIVER == 2 /* I2C */
    uint8_t data[5];
    if (!rg_i2c_read(0x20, -1, &data, 5))
        return false;
    raw_value = data[4];
    charging = data[4] == 255;
#else
    return false;
#endif

    if (!out)
        return true;

    *out = (rg_battery_t){
        .level = RG_MAX(0.f, RG_MIN(100.f, RG_BATTERY_CALC_PERCENT(raw_value))),
        .volts = RG_BATTERY_CALC_VOLTAGE(raw_value),
        .present = present,
        .charging = charging,
    };
    return true;
}

bool rg_input_read_gamepad_raw(uint32_t *out)
{
    uint32_t state = 0;

#if defined(RG_GAMEPAD_ADC_MAP)
    static int old_adc_values[RG_COUNT(keymap_adc)];
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        int value = adc_get_raw(mapping->unit, mapping->channel);
        if (value >= mapping->min && value <= mapping->max)
        {
            if (abs(old_adc_values[i] - value) < RG_GAMEPAD_ADC_FILTER_WINDOW)
                state |= mapping->key;
            // else
            //     RG_LOGD("Rejected input: %d", old_adc_values[i] - value);
            old_adc_values[i] = value;
        }
    }
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        if (gpio_get_level(mapping->num) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    uint32_t buttons = 0;
#if defined(RG_I2C_GPIO_DRIVER)
    int data0 = rg_i2c_gpio_read_port(0), data1 = rg_i2c_gpio_read_port(1);
    if (data0 > -1) // && data1 > -1)
    {
        int p1 = (data1 > -1) ? data1 : 0;
        buttons = (p1 << 8) | (data0);
#elif defined(RG_TARGET_T_DECK_PLUS)
    uint8_t data[5];
    if (rg_i2c_read(T_DECK_KBD_ADDRESS, -1, &data, 5))
    {
        buttons = ((data[0] << 25) | (data[1] << 18) | (data[2] << 11) | ((data[3] & 0xF8) << 4) | (data[4]));
#else
    uint8_t data[5];
    if (rg_i2c_read(RG_I2C_GPIO_ADDR, -1, &data, 5))
    {
        buttons = (data[2] << 8) | (data[1]);
#endif
        for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
        {
            const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
            if (((buttons >> mapping->num) & 1) == mapping->level)
                state |= mapping->key;
        }
    }
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
#ifdef RG_TARGET_SDL2
    int numkeys = 0;
    const uint8_t *keys = SDL_GetKeyboardState(&numkeys);
    for (size_t i = 0; i < RG_COUNT(keymap_kbd); ++i)
    {
        const rg_keymap_kbd_t *mapping = &keymap_kbd[i];
        if (mapping->src < 0 || mapping->src >= numkeys)
            continue;
        if (keys[mapping->src])
            state |= mapping->key;
    }
#else
#warning "not implemented"
#endif
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    rg_usleep(5);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 1);
    rg_usleep(1);
    uint32_t buttons = 0;
    for (int i = 0; i < 16; i++)
    {
        buttons |= gpio_get_level(RG_GPIO_GAMEPAD_DATA) << (15 - i);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 0);
        rg_usleep(1);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
        rg_usleep(1);
    }
    for (size_t i = 0; i < RG_COUNT(keymap_serial); ++i)
    {
        const rg_keymap_serial_t *mapping = &keymap_serial[i];
        if (((buttons >> mapping->num) & 1) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_VIRT_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_virt); ++i)
    {
        if (state == keymap_virt[i].src)
            state = keymap_virt[i].key;
    }
#endif

    if (out)
        *out = state;
    return true;
}

static void input_task(void *arg)
{
    uint8_t debounce[RG_KEY_COUNT];
    uint32_t local_gamepad_state = 0;
    uint32_t state;
    int64_t next_battery_update = 0;

    // Start the task with debounce history full to allow a button held during boot to be detected
    memset(debounce, 0xFF, sizeof(debounce));
    input_task_running = true;

    while (input_task_running)
    {
        if (rg_input_read_gamepad_raw(&state))
        {
            for (int i = 0; i < RG_KEY_COUNT; ++i)
            {
                uint32_t val = ((debounce[i] << 1) | ((state >> i) & 1));
                debounce[i] = val & 0xFF;

                if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1)) == ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1))
                {
                    local_gamepad_state |= (1 << i); // Pressed
                }
                else if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_RELEASE) - 1)) == 0)
                {
                    local_gamepad_state &= ~(1 << i); // Released
                }
            }
            gamepad_state = local_gamepad_state;
        }

        if (rg_system_timer() >= next_battery_update)
        {
            rg_battery_t temp = {0};
            if (rg_input_read_battery_raw(&temp))
            {
                if (fabsf(battery_state.level - temp.level) < RG_BATTERY_UPDATE_THRESHOLD)
                    temp.level = battery_state.level;
                if (fabsf(battery_state.volts - temp.volts) < RG_BATTERY_UPDATE_THRESHOLD_VOLT)
                    temp.volts = battery_state.volts;
            }
            battery_state = temp;
            next_battery_update = rg_system_timer() + 2 * 1000000; // update every 2 seconds
        }

        rg_task_delay(10);
    }

    input_task_running = false;
    gamepad_state = -1;
}

void rg_input_init(void)
{
    RG_ASSERT(!input_task_running, "Input already initialized!");

#if defined(RG_GAMEPAD_ADC_MAP)
    RG_LOGI("Initializing ADC gamepad driver...");
    adc1_config_width(ADC_WIDTH_MAX - 1);
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        if (mapping->unit == ADC_UNIT_1)
            adc1_config_channel_atten(mapping->channel, mapping->atten);
        else if (mapping->unit == ADC_UNIT_2)
            adc2_config_channel_atten(mapping->channel, mapping->atten);
        else
            RG_LOGE("Invalid ADC unit %d!", (int)mapping->unit);
    }
    UPDATE_GLOBAL_MAP(keymap_adc);
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    RG_LOGI("Initializing GPIO gamepad driver...");
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        gpio_set_direction(mapping->num, GPIO_MODE_INPUT);
        if (mapping->pullup && mapping->pulldown)
            gpio_set_pull_mode(mapping->num, GPIO_PULLUP_PULLDOWN);
        else if (mapping->pullup || mapping->pulldown)
            gpio_set_pull_mode(mapping->num, mapping->pullup ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
        else
            gpio_set_pull_mode(mapping->num, GPIO_FLOATING);
    }
    UPDATE_GLOBAL_MAP(keymap_gpio);
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    RG_LOGI("Initializing I2C gamepad driver...");
    rg_i2c_init();
#if defined(RG_I2C_GPIO_DRIVER)
    for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
    {
        const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
        if (mapping->pullup)
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT_PULLUP);
        else
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT);
    }
#elif defined(RG_TARGET_T_DECK_PLUS)
    rg_i2c_write_byte(T_DECK_KBD_ADDRESS, -1, T_DECK_KBD_MODE_RAW_CMD);
#endif
    UPDATE_GLOBAL_MAP(keymap_i2c);
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
    RG_LOGI("Initializing KBD gamepad driver...");
    UPDATE_GLOBAL_MAP(keymap_kbd);
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    RG_LOGI("Initializing SERIAL gamepad driver...");
    gpio_set_direction(RG_GPIO_GAMEPAD_CLOCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_DATA, GPIO_MODE_INPUT);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
    UPDATE_GLOBAL_MAP(keymap_serial);
#endif

#if defined(RG_GAMEPAD_USE_ESPNOW) && defined(ESP_PLATFORM)
    RG_LOGI("Initializing ESP-NOW wireless gamepad driver...");
    espnow_gamepad_init();
#endif


#if RG_BATTERY_DRIVER == 1 /* ADC */
    RG_LOGI("Initializing ADC battery driver...");
    if (RG_BATTERY_ADC_UNIT == ADC_UNIT_1)
    {
        adc1_config_width(ADC_WIDTH_MAX - 1); // there is no adc2_config_width
        adc1_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
    }
    else if (RG_BATTERY_ADC_UNIT == ADC_UNIT_2)
    {
        adc2_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
        esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
    }
    else
    {
        RG_LOGE("Only ADC1 and ADC2 are supported for ADC battery driver!");
    }
#endif

    // The first read returns bogus data in some drivers, waste it.
    rg_input_read_gamepad_raw(NULL);

    // Start background polling
    rg_task_create("rg_input", &input_task, NULL, 3 * 1024, RG_TASK_PRIORITY_6, 1);
    while (gamepad_state == -1)
        rg_task_yield();
    RG_LOGI("Input ready. state=" PRINTF_BINARY_16 "\n", PRINTF_BINVAL_16(gamepad_state));
}

void rg_input_deinit(void)
{
    input_task_running = false;
    // while (gamepad_state != -1)
    //     rg_task_yield();
    RG_LOGI("Input terminated.\n");
}

bool rg_input_key_is_present(rg_key_t mask)
{
    uint32_t mapped = gamepad_mapped;
#if defined(RG_GAMEPAD_USE_ESPNOW) && defined(ESP_PLATFORM)
    if (espnow_gamepad_connected)
    {
        mapped |= (RG_KEY_UP | RG_KEY_DOWN | RG_KEY_LEFT | RG_KEY_RIGHT |
                   RG_KEY_A | RG_KEY_B | RG_KEY_START | RG_KEY_SELECT);
    }
#endif
    return (mapped & mask) == mask;
}

uint32_t rg_input_read_gamepad(void)
{
#ifdef RG_TARGET_SDL2
    SDL_PumpEvents();
#endif
    uint32_t state = gamepad_state;

#if defined(RG_GAMEPAD_USE_ESPNOW) && defined(ESP_PLATFORM)
    portENTER_CRITICAL(&espnow_mux);
    int64_t last = espnow_last_packet_time;
    uint32_t pstate = espnow_gamepad_state;
    portEXIT_CRITICAL(&espnow_mux);

    int64_t diff = rg_system_timer() - last;
    if (diff > 500000)
    {
        if (espnow_gamepad_connected)
        {
            espnow_gamepad_connected = false;
            RG_LOGI("ESP-NOW: Wireless gamepad disconnected.");
        }
        portENTER_CRITICAL(&espnow_mux);
        espnow_gamepad_state = 0;
        portEXIT_CRITICAL(&espnow_mux);
        pstate = 0;
        // Release bonding after 5s so a new controller can auto-bond
        if (diff > 5000000 && espnow_gamepad_bound)
        {
            espnow_gamepad_bound = false;
            espnow_has_bonded_mac = false;
            memset(espnow_bonded_mac, 0, 6);
            RG_LOGI("ESP-NOW: Gamepad slot released for new controllers.");
        }
    }
    state |= pstate;
#endif

    return state;
}

bool rg_input_key_is_pressed(rg_key_t mask)
{
    return (bool)(rg_input_read_gamepad() & mask);
}

bool rg_input_wait_for_key(rg_key_t mask, bool pressed, int timeout_ms)
{
    int64_t expiration = timeout_ms < 0 ? INT64_MAX : (rg_system_timer() + timeout_ms * 1000);
    while (rg_input_key_is_pressed(mask) != pressed)
    {
        if (rg_system_timer() > expiration)
            return false;
        rg_task_delay(10);
    }
    return true;
}

rg_battery_t rg_input_read_battery(void)
{
    return battery_state;
}

const char *rg_input_get_key_name(rg_key_t key)
{
    switch (key)
    {
    case RG_KEY_UP: return "Up";
    case RG_KEY_RIGHT: return "Right";
    case RG_KEY_DOWN: return "Down";
    case RG_KEY_LEFT: return "Left";
    case RG_KEY_SELECT: return "Select";
    case RG_KEY_START: return "Start";
    case RG_KEY_MENU: return "Menu";
    case RG_KEY_OPTION: return "Option";
    case RG_KEY_A: return "A";
    case RG_KEY_B: return "B";
    case RG_KEY_X: return "X";
    case RG_KEY_Y: return "Y";
    case RG_KEY_L: return "Left Shoulder";
    case RG_KEY_R: return "Right Shoulder";
    case RG_KEY_NONE: return "None";
    default: return "Unknown";
    }
}
