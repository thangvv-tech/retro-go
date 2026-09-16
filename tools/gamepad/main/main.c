// Multi-Target Ultra-Low-Latency Wireless Gamepad for Retro-Go (ESP-NOW)
// Pure ESP-IDF application (ESP-IDF v4.4 / v5.x)
// Supports ESP32-C3, ESP32-S3, ESP32 Classic

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "soc/gpio_reg.h"
#include "boards.h"

static const char *TAG = "GAMEPAD";

#ifndef CONFIG_GAMEPAD_SYSTEM_ID
#define CONFIG_GAMEPAD_SYSTEM_ID 0
#endif
#define GAMEPAD_SYSTEM_ID CONFIG_GAMEPAD_SYSTEM_ID

#ifndef CONFIG_GAMEPAD_WIFI_CHANNEL
#define CONFIG_GAMEPAD_WIFI_CHANNEL 1
#endif
#define GAMEPAD_WIFI_CHANNEL CONFIG_GAMEPAD_WIFI_CHANNEL

#ifndef CONFIG_GAMEPAD_POLL_INTERVAL_MS
#define CONFIG_GAMEPAD_POLL_INTERVAL_MS 3
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

typedef struct __attribute__((packed)) {
    uint16_t magic;      // Magic identifier (0x4752)
    uint16_t system_id;  // System/Console ID (0 = all, >0 = isolated room/console)
    uint16_t buttons;    // Bitmask RG_KEY_*
    uint8_t  seq;        // Packet sequence number (0-255)
    uint8_t  player_id;  // 0 = Player 1, 1 = Player 2
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

static void buttons_init(void)
{
    uint64_t pin_mask = 0;
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        pin_mask |= (1ULL << BUTTONS[i].pin);
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
        gpio_wakeup_enable(BUTTONS[i].pin, GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
}

static void wifi_espnow_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Lock WiFi channel to ensure instant connection with receiver
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_channel(GAMEPAD_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));

    // Maximize transmission power (19.5 dBm) for maximum link margin
    esp_wifi_set_max_tx_power(78);

    ESP_ERROR_CHECK(esp_now_init());

    // High-speed 24Mbps OFDM PHY rate reduces packet airtime to ~40us (7.5x faster than 1Mbps CCK)
    esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_24M);

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, BROADCAST_MAC, 6);
    peer_info.channel = GAMEPAD_WIFI_CHANNEL;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    ESP_LOGI(TAG, "ESP-NOW Gamepad TX ready on %s (Channel %d). Broadcasting to Retro-Go.", BOARD_TARGET_NAME, GAMEPAD_WIFI_CHANNEL);
}

// Ultra-fast GPIO read using hardware register (1 CPU cycle for all 22 GPIOs)
static inline uint16_t read_buttons_raw(void)
{
    uint32_t gpio_val = REG_READ(GPIO_IN_REG);
    uint16_t keys = 0;

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        // Active LOW: 0 = pressed
        if (!(gpio_val & (1UL << BUTTONS[i].pin))) {
            keys |= BUTTONS[i].key;
        }
    }
    return keys;
}

static uint8_t init_player_id(void)
{
    nvs_handle_t handle;
    uint8_t id = 0;
    esp_err_t err = nvs_open("gamepad", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        // Allow pull-up stabilization and user button hold to register reliably
        vTaskDelay(pdMS_TO_TICKS(20));
        uint16_t boot_keys = read_buttons_raw();
        if (boot_keys & RG_KEY_B) {
            id = 1; // Button B held at boot -> Player 2
            nvs_set_u8(handle, "player_id", id);
            nvs_commit(handle);
            ESP_LOGI(TAG, "Configured as Player 2 (B held at boot)");
        } else if (boot_keys & RG_KEY_A) {
            id = 0; // Button A held at boot -> Player 1
            nvs_set_u8(handle, "player_id", id);
            nvs_commit(handle);
            ESP_LOGI(TAG, "Configured as Player 1 (A held at boot)");
        } else {
            if (nvs_get_u8(handle, "player_id", &id) != ESP_OK) {
                id = 0;
            }
            ESP_LOGI(TAG, "Active Player ID: %d (Player %d)", id, id + 1);
        }
        nvs_close(handle);

        if (boot_keys & (RG_KEY_A | RG_KEY_B)) {
            int wait_count = 0;
            while ((read_buttons_raw() & (RG_KEY_A | RG_KEY_B)) && wait_count++ < 200) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
    return id;
}

static void indicate_player_led(uint8_t player_id)
{
#ifdef PIN_LED
    // Onboard status LED: Blink 1x for Player 1, 2x for Player 2
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    int blinks = (player_id == 0) ? 1 : 2;
    for (int i = 0; i < blinks; i++) {
        gpio_set_level(PIN_LED, 0); // Active LOW on most ESP boards
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(PIN_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
#endif
}

void app_main(void)
{
    wifi_espnow_init();
    buttons_init();

    uint8_t player_id = init_player_id();
    indicate_player_led(player_id);
    buttons_init(); // Re-arm GPIO 8 as input with pull-up for Button L

    uint16_t debounced_state = 0;
    uint16_t last_sample = 0;
    uint16_t last_sent_state = 0xFFFF; // Force initial packet send
    int64_t last_send_time = 0;
    int64_t last_activity_time = esp_timer_get_time() / 1000;

    gamepad_packet_t packet = {
        .magic = GAMEPAD_MAGIC,
        .system_id = GAMEPAD_SYSTEM_ID,
        .buttons = 0,
        .seq = 0,
        .player_id = player_id,
    };

    while (1) {
        // 1. Single-cycle hardware register read
        uint16_t current_sample = read_buttons_raw();

        // 2. Asymmetric debounce: 0ms instant trigger on press, 2-sample filter on release
        uint16_t newly_pressed = current_sample & ~debounced_state;
        if (newly_pressed) {
            debounced_state |= newly_pressed;
        }
        uint16_t candidate_release = debounced_state & ~current_sample;
        if (candidate_release) {
            // Only clear bit if released across 2 consecutive samples (filters contact bounce)
            debounced_state &= ~(candidate_release & ~last_sample);
        }
        last_sample = current_sample;

        int64_t now = esp_timer_get_time() / 1000; // ms

        // Track activity for sleep timeout
        if (debounced_state != 0) {
            last_activity_time = now;
        }

        // 3. Transmit immediately on state change, or periodically on heartbeat interval
        bool state_changed = (debounced_state != last_sent_state);
        bool heartbeat_due = (now - last_send_time >= HEARTBEAT_INTERVAL_MS);

        if (state_changed || heartbeat_due) {
            packet.buttons = debounced_state;
            packet.seq = packet_seq++;
            packet.player_id = player_id;

            esp_now_send(BROADCAST_MAC, (const uint8_t *)&packet, sizeof(packet));

            last_sent_state = debounced_state;
            last_send_time = now;
        }

        // 4. Power management: Enter light-sleep if idle for 60 seconds
        if (debounced_state == 0 && (now - last_activity_time > LIGHT_SLEEP_TIMEOUT_MS)) {
            ESP_LOGI(TAG, "Entering light-sleep mode (idle). Press any button to wake up.");
            // Send clear state before sleeping
            packet.buttons = 0;
            packet.seq = packet_seq++;
            packet.player_id = player_id;
            esp_now_send(BROADCAST_MAC, (const uint8_t *)&packet, sizeof(packet));

            // Flush WiFi buffers
            vTaskDelay(pdMS_TO_TICKS(10));

            // Sleep until any button GPIO is pulled LOW
            esp_light_sleep_start();

            // Woke up on button press
            now = esp_timer_get_time() / 1000;
            last_activity_time = now;
            last_sent_state = 0xFFFF; // Force instant transmission on wake
            ESP_LOGI(TAG, "Woke up from light sleep.");
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
