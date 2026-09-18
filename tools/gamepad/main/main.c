// Multi-Target Ultra-Low-Latency Wireless Gamepad for Retro-Go (ESP-NOW + Web Controller)
// Pure ESP-IDF application (ESP-IDF v4.4 / v5.x)
// Supports ESP32-C3, ESP32-S3, ESP32 Classic
// Auto Channel Hopping & Proactive Channel Handover with Explicit Bonded Pairing

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_mac.h"
#include "soc/gpio_reg.h"
#include "boards.h"
#include "web_page.h"

static const char *TAG = "GAMEPAD";

#ifndef CONFIG_GAMEPAD_SYSTEM_ID
#define CONFIG_GAMEPAD_SYSTEM_ID 0
#endif
#define GAMEPAD_SYSTEM_ID CONFIG_GAMEPAD_SYSTEM_ID

#ifndef CONFIG_GAMEPAD_WIFI_CHANNEL
#define CONFIG_GAMEPAD_WIFI_CHANNEL 1
#endif

#ifndef CONFIG_GAMEPAD_POLL_INTERVAL_MS
#define CONFIG_GAMEPAD_POLL_INTERVAL_MS 1
#endif
#define POLL_INTERVAL_MS CONFIG_GAMEPAD_POLL_INTERVAL_MS

#ifndef CONFIG_GAMEPAD_HEARTBEAT_INTERVAL_MS
#define CONFIG_GAMEPAD_HEARTBEAT_INTERVAL_MS 16
#endif
#define HEARTBEAT_INTERVAL_MS CONFIG_GAMEPAD_HEARTBEAT_INTERVAL_MS

#ifndef CONFIG_GAMEPAD_SLEEP_TIMEOUT_MS
#define CONFIG_GAMEPAD_SLEEP_TIMEOUT_MS 60000
#endif
#define LIGHT_SLEEP_TIMEOUT_MS CONFIG_GAMEPAD_SLEEP_TIMEOUT_MS

#define GAMEPAD_MAGIC 0x4752 // 'R', 'G'

#define CHANNEL_HOP_INTERVAL_MS  120  // Dwell time per channel during search (120ms)
#define CHANNEL_SYNC_TIMEOUT_MS  4000 // Timeout before triggering auto-scan if ACK lost (4.0s - covers console reboot)

#define RG_ESPNOW_CMD_ACK          0xFFFF // Normal state ACK / PONG
#define RG_ESPNOW_CMD_PAIR_REQ     0xFFFE // Gamepad -> Console: Request pairing
#define RG_ESPNOW_CMD_PAIR_ACK     0xFFFD // Console -> Gamepad: Confirm pairing
#define RG_ESPNOW_CMD_CHAN_SWITCH  0xFFFC // Console -> Gamepad: Switch channel immediately
#define RG_ESPNOW_CMD_UNPAIR       0xFFFB // Console -> Gamepad: Unpair command

typedef struct __attribute__((packed)) {
    uint16_t magic;          // Magic identifier (0x4752)
    uint16_t system_id;      // System/Console ID (0 = all, >0 = isolated room/console)
    uint16_t buttons;        // Bitmask RG_KEY_* or RG_ESPNOW_CMD_*
    uint8_t  seq;            // Packet sequence number (0-255)
    uint8_t  player_id;      // 0 = Player 1, 1 = Player 2
    uint8_t  console_mac[6]; // Paired Console STA MAC (all 0 if unpaired)
    uint8_t  channel;        // Primary channel hint
    uint8_t  reserved;       // Alignment padding
} gamepad_packet_t;

typedef struct {
    gpio_num_t pin;
    uint16_t key;
} button_map_t;

static const button_map_t BUTTONS[] = {
    {PIN_UP,     RG_KEY_UP},
    {PIN_DOWN,   RG_KEY_DOWN},
    {PIN_LEFT,   RG_KEY_LEFT},
    {PIN_RIGHT,  RG_KEY_RIGHT},
    {PIN_A,      RG_KEY_A},
    {PIN_B,      RG_KEY_B},
    {PIN_X,      RG_KEY_X},
    {PIN_Y,      RG_KEY_Y},
    {PIN_L,      RG_KEY_L},
    {PIN_R,      RG_KEY_R},
    {PIN_SELECT, RG_KEY_SELECT},
    {PIN_START,  RG_KEY_START},
    {PIN_MENU,   RG_KEY_MENU},
};

#define BUTTON_COUNT (sizeof(BUTTONS) / sizeof(BUTTONS[0]))

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t packet_seq = 0;
static uint8_t active_player_id = 0;

// Pairing & MAC Whitelist State
static uint8_t paired_console_mac[6] = {0};
static bool is_paired = false;

// Channel sync state
static volatile int64_t last_ack_time = 0;
static volatile int64_t last_hop_time = 0;
static volatile bool channel_locked = false;
static uint8_t current_channel = CONFIG_GAMEPAD_WIFI_CHANNEL;
static uint8_t saved_channel = CONFIG_GAMEPAD_WIFI_CHANNEL;
static int64_t stable_channel_since = 0;

// Web virtual gamepad state (always enabled as requested)
static bool web_controller_enabled = true;
static volatile uint16_t web_buttons = 0;
static int64_t web_last_activity = 0;

static void save_bonding_to_nvs(const uint8_t *mac)
{
    if (is_paired && memcmp(paired_console_mac, mac, 6) == 0)
        return;

    nvs_handle_t handle;
    if (nvs_open("gamepad", NVS_READWRITE, &handle) == ESP_OK) {
        uint8_t cur_mac[6];
        size_t len = sizeof(cur_mac);
        if (nvs_get_blob(handle, "console_mac", cur_mac, &len) != ESP_OK ||
            len != 6 || memcmp(cur_mac, mac, 6) != 0) {
            nvs_set_blob(handle, "console_mac", mac, 6);
            nvs_commit(handle);
        }
        nvs_close(handle);
        memcpy(paired_console_mac, mac, 6);
        is_paired = true;
    }
}

static bool load_bonding_from_nvs(void)
{
    nvs_handle_t handle;
    size_t len = 6;
    bool ok = false;
    if (nvs_open("gamepad", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_blob(handle, "console_mac", paired_console_mac, &len) == ESP_OK && len == 6) {
            uint8_t zero_mac[6] = {0};
            if (memcmp(paired_console_mac, zero_mac, 6) != 0) {
                ok = true;
            }
        }
        nvs_close(handle);
    }
    return ok;
}

static void clear_bonding_nvs(void)
{
    if (!is_paired) {
        memset(paired_console_mac, 0, 6);
        return;
    }
    nvs_handle_t handle;
    if (nvs_open("gamepad", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "console_mac");
        nvs_commit(handle);
        nvs_close(handle);
    }
    memset(paired_console_mac, 0, 6);
    is_paired = false;
}

static void save_channel_to_nvs(uint8_t ch)
{
    if (ch == saved_channel || ch < 1 || ch > 13)
        return; // Skip redundant writes
    nvs_handle_t handle;
    if (nvs_open("gamepad", NVS_READWRITE, &handle) == ESP_OK) {
        uint8_t cur_ch = 0;
        if (nvs_get_u8(handle, "wifi_chan", &cur_ch) != ESP_OK || cur_ch != ch) {
            nvs_set_u8(handle, "wifi_chan", ch);
            nvs_commit(handle);
        }
        nvs_close(handle);
        saved_channel = ch;
    }
}

static uint8_t load_channel_from_nvs(void)
{
    nvs_handle_t handle;
    uint8_t ch = CONFIG_GAMEPAD_WIFI_CHANNEL;
    if (nvs_open("gamepad", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_u8(handle, "wifi_chan", &ch) != ESP_OK || ch < 1 || ch > 13) {
            ch = CONFIG_GAMEPAD_WIFI_CHANNEL;
        }
        nvs_close(handle);
    }
    return ch;
}

static void set_gamepad_channel(uint8_t ch)
{
    if (ch < 1 || ch > 13)
        return;
    current_channel = ch;

    if (web_controller_enabled) {
        wifi_config_t ap_config;
        if (esp_wifi_get_config(WIFI_IF_AP, &ap_config) == ESP_OK) {
            if (ap_config.ap.channel != ch) {
                ap_config.ap.channel = ch;
                esp_wifi_set_config(WIFI_IF_AP, &ap_config);
            }
        }
    }
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

typedef enum {
    GP_EV_PAIR_ACK,
    GP_EV_ACK,
    GP_EV_CHAN_SWITCH,
    GP_EV_UNPAIR,
} gamepad_rx_ev_type_t;

typedef struct {
    gamepad_rx_ev_type_t type;
    uint8_t console_mac[6];
    uint8_t channel;
} gamepad_rx_ev_t;

static QueueHandle_t rx_ev_queue = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    const uint8_t *src_mac = esp_now_info->src_addr;
    (void)src_mac;
#else
static void espnow_recv_cb(const uint8_t *src_mac, const uint8_t *data, int data_len)
{
    (void)src_mac;
#endif
    if (data_len < (int)sizeof(gamepad_packet_t))
        return;

    const gamepad_packet_t *packet = (const gamepad_packet_t *)data;
    if (packet->magic != GAMEPAD_MAGIC)
        return;

#if GAMEPAD_SYSTEM_ID > 0
    if (packet->system_id != GAMEPAD_SYSTEM_ID)
        return;
#endif

    int64_t now_ms = esp_timer_get_time() / 1000;

    // Handle pairing confirmation from console
    if (packet->buttons == RG_ESPNOW_CMD_PAIR_ACK) {
        last_ack_time = now_ms;
        channel_locked = true;
        if (rx_ev_queue) {
            gamepad_rx_ev_t ev = {
                .type = GP_EV_PAIR_ACK,
                .channel = packet->channel,
            };
            memcpy(ev.console_mac, packet->console_mac, 6);
            xQueueSend(rx_ev_queue, &ev, 0);
        }
        return;
    }

    // Handle unpair notification from console
    if (packet->buttons == RG_ESPNOW_CMD_UNPAIR) {
        if (is_paired && memcmp(packet->console_mac, paired_console_mac, 6) == 0) {
            if (rx_ev_queue) {
                gamepad_rx_ev_t ev = {
                    .type = GP_EV_UNPAIR,
                };
                xQueueSend(rx_ev_queue, &ev, 0);
            }
        }
        return;
    }

    // For normal ACKs and CHAN_SWITCH, verify console MAC if paired
    if (is_paired) {
        if (memcmp(packet->console_mac, paired_console_mac, 6) != 0) {
            return; // Ignore responses from non-paired console
        }
    }

    if (packet->buttons == RG_ESPNOW_CMD_ACK || packet->buttons == RG_ESPNOW_CMD_CHAN_SWITCH) {
        last_ack_time = now_ms;
        channel_locked = true;
        if (rx_ev_queue) {
            gamepad_rx_ev_t ev = {
                .type = (packet->buttons == RG_ESPNOW_CMD_ACK) ? GP_EV_ACK : GP_EV_CHAN_SWITCH,
                .channel = packet->channel,
            };
            memcpy(ev.console_mac, packet->console_mac, 6);
            xQueueSend(rx_ev_queue, &ev, 0);
        }
    }
}

static void buttons_init(void)
{
    uint64_t pin_mask = 0;
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        if (BUTTONS[i].pin != GPIO_NUM_NC && BUTTONS[i].pin >= 0) {
            pin_mask |= (1ULL << BUTTONS[i].pin);
        }
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Configure GPIO wakeup for light sleep
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        if (BUTTONS[i].pin != GPIO_NUM_NC && BUTTONS[i].pin >= 0) {
            gpio_wakeup_enable(BUTTONS[i].pin, GPIO_INTR_LOW_LEVEL);
        }
    }
    esp_sleep_enable_gpio_wakeup();
}

// Ultra-fast GPIO read using hardware register (1 CPU cycle for all GPIOs)
static inline uint16_t read_buttons_raw(void)
{
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    uint32_t inv = ~REG_READ(GPIO_IN_REG);
    uint16_t keys = 0;

    // D-Pad: GPIO 0 (UP), GPIO 1 (DOWN), GPIO 2 (LEFT), GPIO 3 (RIGHT)
    if (inv & (1 << PIN_UP))    keys |= RG_KEY_UP;
    if (inv & (1 << PIN_DOWN))  keys |= RG_KEY_DOWN;
    if (inv & (1 << PIN_LEFT))  keys |= RG_KEY_LEFT;
    if (inv & (1 << PIN_RIGHT)) keys |= RG_KEY_RIGHT;

    // Action buttons: GPIO 4 (A), 5 (B), 6 (X), 7 (Y) -> shifted left by 4 maps directly to bits 8..11
    keys |= (uint16_t)((inv & 0xF0) << 4);

    // Shoulder buttons
    if (PIN_L >= 0 && (inv & (1 << (PIN_L >= 0 ? PIN_L : 0))))     keys |= RG_KEY_L;
    if (PIN_R >= 0 && (inv & (1 << (PIN_R >= 0 ? PIN_R : 0))))     keys |= RG_KEY_R;

    // Select / Start: GPIO 20 (SELECT), GPIO 21 (START) -> shifted right by 16 maps directly to bits 4..5
    keys |= (uint16_t)((inv & (0x3 << 20)) >> 16);

    if (PIN_MENU >= 0 && (inv & (1 << (PIN_MENU >= 0 ? PIN_MENU : 0)))) keys |= RG_KEY_MENU;

    // Virtual MENU combo: SELECT + UP (frees physical BOOT pin GPIO 9)
    if ((keys & (RG_KEY_SELECT | RG_KEY_UP)) == (RG_KEY_SELECT | RG_KEY_UP)) {
        keys &= ~(RG_KEY_SELECT | RG_KEY_UP);
        keys |= RG_KEY_MENU;
    }

    return keys;
#else
    uint32_t gpio_val0 = REG_READ(GPIO_IN_REG);
#if defined(GPIO_IN1_REG)
    uint32_t gpio_val1 = REG_READ(GPIO_IN1_REG);
#endif
    uint16_t keys = 0;

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        gpio_num_t pin = BUTTONS[i].pin;
        if (pin == GPIO_NUM_NC || pin < 0)
            continue;
        bool pressed = false;
#if defined(GPIO_IN1_REG)
        if (pin >= 32) {
            pressed = !(gpio_val1 & (1UL << (pin - 32)));
        } else {
            pressed = !(gpio_val0 & (1UL << pin));
        }
#else
        pressed = !(gpio_val0 & (1UL << pin));
#endif
        if (pressed) {
            keys |= BUTTONS[i].key;
        }
    }
    return keys;
#endif
}

static void blink_led_pattern(int count, int on_ms, int off_ms)
{
#ifdef PIN_LED
    for (int i = 0; i < count; i++) {
        gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
        gpio_set_level(PIN_LED, 0); // Active LOW: LED ON
        vTaskDelay(pdMS_TO_TICKS(on_ms));

        // LED OFF: Open-Drain Hi-Z via INPUT with pull-up.
        // Never drive push-pull HIGH (3.3V) to prevent shorting if Button L (GPIO 8) is pressed!
        gpio_set_direction(PIN_LED, GPIO_MODE_INPUT);
        gpio_set_pull_mode(PIN_LED, GPIO_PULLUP_ONLY);
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
#endif
}

static void init_gamepad_pairing(void)
{
    // Allow pull-up stabilization and user button hold to register reliably
    vTaskDelay(pdMS_TO_TICKS(20));
    uint16_t boot_keys = read_buttons_raw();

    // Boot combo: SELECT + START held together clears bonding bond
    if ((boot_keys & (RG_KEY_SELECT | RG_KEY_START)) == (RG_KEY_SELECT | RG_KEY_START)) {
        clear_bonding_nvs();
        blink_led_pattern(5, 60, 60);
        ESP_LOGW(TAG, "Boot combo: Pairing bond cleared! Entering discovery pairing mode.");
    } else if (boot_keys & RG_KEY_START) {
        web_controller_enabled = true;
        blink_led_pattern(3, 80, 80);
        ESP_LOGI(TAG, "Boot key: Web Controller (SoftAP) enabled on demand!");
    }

    active_player_id = 0;

    if (boot_keys & (RG_KEY_SELECT | RG_KEY_START)) {
        int wait_count = 0;
        while ((read_buttons_raw() & (RG_KEY_SELECT | RG_KEY_START)) && wait_count++ < 200) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void indicate_ready_led(void)
{
    blink_led_pattern(1, 150, 100);
}

// -----------------------------------------------------------------------------
// Web UI & WebSocket Server
// -----------------------------------------------------------------------------
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Web client connected to WebSocket");
        web_last_activity = esp_timer_get_time();
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t buf[16];
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    ws_pkt.payload = buf;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf));
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len >= 2) {
        if (buf[0] == 0xAA) {
            // Control Command from Web UI
            if (buf[1] == 0x02) {
                // Trigger Re-Pairing
                clear_bonding_nvs();
                channel_locked = false;
                set_gamepad_channel(current_channel);
                last_hop_time = esp_timer_get_time() / 1000;
                ESP_LOGI(TAG, "Web triggered Re-Pairing mode!");
            }
        } else {
            web_buttons = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
            web_last_activity = esp_timer_get_time();
        }

        // Send status telemetry back to Web client
        uint8_t status_buf[6] = {
            0xBB,
            is_paired ? 1 : 0,
            active_player_id,
            current_channel,
            channel_locked ? 1 : 0,
            0
        };
        httpd_ws_frame_t resp_pkt = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = status_buf,
            .len = sizeof(status_buf),
        };
        httpd_ws_send_frame(req, &resp_pkt);
    }
    return ESP_OK;
}

static esp_err_t http_get_index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = http_get_index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t ws_uri = {
            .uri          = "/ws",
            .method       = HTTP_GET,
            .handler      = ws_handler,
            .user_ctx     = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_uri);

        ESP_LOGI(TAG, "Web Gamepad UI started on http://192.168.4.1");
    }
    return server;
}

static void wifi_espnow_init(uint8_t player_id)
{
    is_paired = load_bonding_from_nvs();
    current_channel = load_channel_from_nvs();
    saved_channel = current_channel;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    if (web_controller_enabled) {
        esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    if (web_controller_enabled) {
        wifi_config_t ap_config = {
            .ap = {
                .channel = current_channel,
                .password = "",
                .max_connection = 4,
                .authmode = WIFI_AUTH_OPEN,
            },
        };
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "RetroGo-Pad-%02X%02X", mac[4], mac[5]);
        ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    set_gamepad_channel(current_channel);

    // Continuous reception without modem sleep drops
    esp_wifi_set_ps(WIFI_PS_NONE);
    // Maximize transmission power (19.5 dBm) for solid link margin
    esp_wifi_set_max_tx_power(78);

    ESP_ERROR_CHECK(esp_now_init());

    // High-speed 24Mbps OFDM PHY rate reduces packet airtime to ~40us
    esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_24M);

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, BROADCAST_MAC, 6);
    peer_info.channel = 0; // 0 = follow interface channel dynamically
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    ESP_LOGI(TAG, "ESP-NOW Gamepad TX ready on Channel %d (Web Controller: %s). Paired: %s",
             current_channel, web_controller_enabled ? "ON" : "OFF (Standby)", is_paired ? "YES" : "NO (Discovery Mode)");
}

void app_main(void)
{
    // 1. Initialize NVS flash first before ANY NVS access (boot combo unpairing, bonding load)
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    rx_ev_queue = xQueueCreate(8, sizeof(gamepad_rx_ev_t));

    buttons_init();

    init_gamepad_pairing();
    indicate_ready_led();
    buttons_init(); // Re-arm GPIO as input with pull-up

    wifi_espnow_init(0);
    if (web_controller_enabled) {
        start_webserver();
    }

    uint16_t debounced_gpio_state = 0;
    uint16_t last_sample = 0;
    uint16_t last_sent_state = 0xFFFF; // Force initial packet send
    int64_t last_send_time = 0;
    int64_t last_activity_time = esp_timer_get_time() / 1000;

    gamepad_packet_t packet = {
        .magic = GAMEPAD_MAGIC,
        .system_id = GAMEPAD_SYSTEM_ID,
        .buttons = 0,
        .seq = 0,
        .player_id = active_player_id,
        .channel = current_channel,
        .reserved = 0,
    };
    memcpy(packet.console_mac, paired_console_mac, 6);

    while (1) {
        int64_t now_us = esp_timer_get_time();
        int64_t now_ms = now_us / 1000;

        // Process deferred events from ESP-NOW recv callback (NVS write, channel switch)
        gamepad_rx_ev_t rx_ev;
        while (rx_ev_queue && xQueueReceive(rx_ev_queue, &rx_ev, 0) == pdTRUE) {
            if (rx_ev.type == GP_EV_PAIR_ACK) {
                save_bonding_to_nvs(rx_ev.console_mac);
                memcpy(packet.console_mac, paired_console_mac, 6);
                uint8_t ch = (rx_ev.channel >= 1 && rx_ev.channel <= 13) ? rx_ev.channel : current_channel;
                set_gamepad_channel(ch);
                blink_led_pattern(2, 100, 100);
                ESP_LOGI(TAG, "Bonded successfully with Console %02X:%02X:%02X:%02X:%02X:%02X on Channel %d",
                         rx_ev.console_mac[0], rx_ev.console_mac[1], rx_ev.console_mac[2],
                         rx_ev.console_mac[3], rx_ev.console_mac[4], rx_ev.console_mac[5], current_channel);
            } else if (rx_ev.type == GP_EV_UNPAIR) {
                clear_bonding_nvs();
                channel_locked = false;
                last_hop_time = now_ms;
                blink_led_pattern(5, 60, 60);
                ESP_LOGW(TAG, "Unpaired by Console command! Entering discovery mode.");
            } else if (rx_ev.type == GP_EV_ACK || rx_ev.type == GP_EV_CHAN_SWITCH) {
                if (rx_ev.channel >= 1 && rx_ev.channel <= 13 && rx_ev.channel != current_channel) {
                    set_gamepad_channel(rx_ev.channel);
                    ESP_LOGI(TAG, "%s to Channel %d instructed by Console.",
                             (rx_ev.type == GP_EV_ACK) ? "Console sync" : "Proactively switched", current_channel);
                }
            }
        }

        // 1. Single-cycle hardware register read for physical buttons
        uint16_t current_sample = read_buttons_raw();

        // 2. Asymmetric debounce: 0ms instant trigger on press, 2-sample filter on release
        uint16_t newly_pressed = current_sample & ~debounced_gpio_state;
        if (newly_pressed) {
            debounced_gpio_state |= newly_pressed;
        }
        uint16_t candidate_release = debounced_gpio_state & ~current_sample;
        if (candidate_release) {
            debounced_gpio_state &= ~(candidate_release & ~last_sample);
        }
        last_sample = current_sample;

        // 3. Safety Watchdog: clear web buttons if client disconnected or idle > 1.5s
        if (web_controller_enabled) {
            if (web_buttons != 0 && (now_us - web_last_activity > 1500000)) {
                web_buttons = 0;
            }
        } else {
            web_buttons = 0;
        }

        // 4. Merge Physical GPIO buttons + Web Virtual buttons (if enabled)
        uint16_t current_buttons = debounced_gpio_state | web_buttons;

        // Runtime 3s hold of SELECT + START triggers unpair on-the-fly
        static int64_t unpair_hold_start = 0;
        if ((current_buttons & (RG_KEY_SELECT | RG_KEY_START)) == (RG_KEY_SELECT | RG_KEY_START)) {
            if (unpair_hold_start == 0) {
                unpair_hold_start = now_ms;
            } else if (now_ms - unpair_hold_start >= 3000) {
                clear_bonding_nvs();
                channel_locked = false;
                last_hop_time = now_ms;
                blink_led_pattern(5, 60, 60);
                ESP_LOGW(TAG, "Runtime 3s hold: Pairing cleared! Entering discovery mode.");
                unpair_hold_start = 0;
                while ((read_buttons_raw() & (RG_KEY_SELECT | RG_KEY_START))) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        } else {
            unpair_hold_start = 0;
        }

        // Track activity for sleep timeout (physical buttons or web activity)
        if (current_buttons != 0 || (now_us - web_last_activity < 60000000LL)) {
            last_activity_time = now_ms;
        }

        // 5. Pairing & Auto Channel Hopping State Machine
        if (!is_paired) {
            // Unpaired: Hop and transmit PAIR_REQ (gentle 500ms if SoftAP enabled to let phone connect)
            int hop_interval = web_controller_enabled ? 500 : CHANNEL_HOP_INTERVAL_MS;
            if (now_ms - last_hop_time >= hop_interval) {
                current_channel = (current_channel % 13) + 1; // 1..13
                set_gamepad_channel(current_channel);
                last_hop_time = now_ms;

                packet.buttons = RG_ESPNOW_CMD_PAIR_REQ;
                packet.seq = packet_seq++;
                packet.player_id = active_player_id;
                packet.channel = current_channel;
                memset(packet.console_mac, 0, 6);
                esp_now_send(BROADCAST_MAC, (const uint8_t *)&packet, sizeof(packet));
            }
        } else {
            if (channel_locked) {
                // Check if connection timed out (Console switched WiFi / shut down)
                if (now_ms - last_ack_time > CHANNEL_SYNC_TIMEOUT_MS) {
                    channel_locked = false;
                    last_hop_time = now_ms;
                    if (current_channel != saved_channel) {
                        set_gamepad_channel(saved_channel);
                    }
                    ESP_LOGW(TAG, "Console sync lost (%d ms). Scanning channels...", CHANNEL_SYNC_TIMEOUT_MS);
                }
            } else {
                // Searching channels: gentle dwell time (1500ms) if SoftAP enabled to keep phone WiFi alive
                int hop_interval = web_controller_enabled ? 1500 : CHANNEL_HOP_INTERVAL_MS;
                if (now_ms - last_hop_time >= hop_interval) {
                    current_channel = (current_channel % 13) + 1;
                    set_gamepad_channel(current_channel);
                    last_hop_time = now_ms;
                    last_sent_state = 0xFFFF; // Force probe packet transmission on each channel
                    last_send_time = 0;       // Transmit probe immediately
                }
            }
        }

        // 6. Flash wear-out protection: Debounce NVS commit by requiring 3 seconds continuous channel stability
        if (channel_locked) {
            if (current_channel != saved_channel) {
                if (stable_channel_since == 0) {
                    stable_channel_since = now_ms;
                } else if (now_ms - stable_channel_since >= 3000) {
                    save_channel_to_nvs(current_channel);
                    ESP_LOGI(TAG, "Locked to Console on Channel %d (saved to NVS after 3s stability).", current_channel);
                    stable_channel_since = 0;
                }
            } else {
                stable_channel_since = 0;
            }
        } else {
            stable_channel_since = 0;
        }

        // 7. Transmit immediately on state change, or periodically on heartbeat interval
        if (is_paired) {
            bool state_changed = (current_buttons != last_sent_state);
            bool heartbeat_due = (now_ms - last_send_time >= HEARTBEAT_INTERVAL_MS);

            if (state_changed || heartbeat_due) {
                packet.buttons = current_buttons;
                packet.seq = packet_seq++;
                packet.player_id = active_player_id;
                packet.channel = current_channel;
                memcpy(packet.console_mac, paired_console_mac, 6);

                esp_now_send(BROADCAST_MAC, (const uint8_t *)&packet, sizeof(packet));

                last_sent_state = current_buttons;
                last_send_time = now_ms;
            }
        }

        // 8. Power management: Enter light-sleep only when Web Controller (SoftAP) is disabled
        if (!web_controller_enabled) {
            int64_t idle_timeout = channel_locked ? LIGHT_SLEEP_TIMEOUT_MS : 15000;
            if (current_buttons == 0 && (now_ms - last_activity_time > idle_timeout)) {
                ESP_LOGI(TAG, "Entering light-sleep mode (idle). Press any button to wake up.");
                if (is_paired) {
                    packet.buttons = 0;
                    packet.seq = packet_seq++;
                    packet.player_id = active_player_id;
                    packet.channel = current_channel;
                    memcpy(packet.console_mac, paired_console_mac, 6);
                    esp_now_send(BROADCAST_MAC, (const uint8_t *)&packet, sizeof(packet));
                }

                vTaskDelay(pdMS_TO_TICKS(20));
                esp_wifi_stop();
                esp_light_sleep_start();

                // Woke up from light sleep: restore buttons and Wi-Fi stack
                buttons_init();
                esp_wifi_start();
                set_gamepad_channel(current_channel);
                esp_wifi_set_ps(WIFI_PS_NONE);
                esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_24M);

                now_ms = esp_timer_get_time() / 1000;
                last_activity_time = now_ms;
                web_last_activity = esp_timer_get_time();
                last_sent_state = 0xFFFF; // Force instant transmission on wake
                ESP_LOGI(TAG, "Woke up from light sleep.");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
