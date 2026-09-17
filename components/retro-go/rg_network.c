#include "rg_system.h"
#include "rg_network.h"
#include "rg_input.h"

#include <stdlib.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <esp_http_client.h>
#include <nvs_flash.h>
#include <lwip/err.h>
#include <lwip/sys.h>

#define SETTING_WIFI_ENABLE   "Wifi"
#define SETTING_WIFI_MODE     "Mode"
#define SETTING_WIFI_SSID     "SSID"
#define SETTING_WIFI_PASSWORD "Pass"
#define SETTING_WIFI_CHANNEL  "Chan"

#define TRY(x) do { if ((err = (x)) != ESP_OK) { RG_LOGE(#x " = 0x%x\n", err); goto fail; } } while (0)

#ifdef RG_ENABLE_NETWORKING
static rg_wifi_config_t wifi_config;
static rg_network_state_t network_state = RG_NETWORK_DISABLED;
static bool wifi_user_started = false;
static esp_netif_t *netif_sta = NULL;
static esp_netif_t *netif_ap = NULL;
static esp_netif_t *netif = NULL;

static int wifi_retry_count = 0;
#define WIFI_MAX_RETRY 5

static void network_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_STOP || event_id == WIFI_EVENT_AP_STOP)
        {
            network_state = RG_NETWORK_DISCONNECTED;
            wifi_user_started = false;
            wifi_retry_count = 0;
            rg_system_event(RG_EVENT_NETWORK_DISCONNECTED, NULL);
            RG_LOGI("Wifi stopped.");
        }
        else if (event_id == WIFI_EVENT_STA_START)
        {
            if (wifi_user_started && wifi_config.ssid[0] && network_state != RG_NETWORK_CONNECTING && network_state != RG_NETWORK_CONNECTED)
            {
                network_state = RG_NETWORK_CONNECTING;
                RG_LOGI("Connecting to '%s' (channel: %d)...", wifi_config.ssid, wifi_config.channel);
                esp_wifi_connect();
            }
        }
        else if (event_id == WIFI_EVENT_STA_CONNECTED)
        {
            esp_wifi_set_ps(WIFI_PS_NONE);
            #if defined(RG_GAMEPAD_USE_ESPNOW)
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            if (event && event->channel > 0)
            {
                rg_input_espnow_notify_channel_switch(event->channel);
            }
            #endif
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            int reason = event ? event->reason : -1;
            if (wifi_user_started && wifi_config.ssid[0])
            {
                if (++wifi_retry_count <= WIFI_MAX_RETRY)
                {
                    network_state = RG_NETWORK_CONNECTING;
                    RG_LOGW("Disconnected from AP (reason: %d). Reconnecting (%d/%d)...",
                            reason, wifi_retry_count, WIFI_MAX_RETRY);
                    rg_system_event(RG_EVENT_NETWORK_DISCONNECTED, NULL);
                    esp_wifi_connect();
                }
                else
                {
                    wifi_retry_count = 0;
                    network_state = RG_NETWORK_DISCONNECTED;
                    wifi_user_started = false;
                    RG_LOGW("Connection to AP failed after %d attempts (last reason: %d).",
                            WIFI_MAX_RETRY, reason);
                    rg_system_event(RG_EVENT_NETWORK_DISCONNECTED, NULL);
                }
            }
            else
            {
                wifi_retry_count = 0;
                network_state = RG_NETWORK_DISCONNECTED;
                netif = NULL;
                rg_system_event(RG_EVENT_NETWORK_DISCONNECTED, NULL);
            }
        }
        else if (event_id == WIFI_EVENT_AP_START)
        {
            if (wifi_user_started)
            {
                network_state = RG_NETWORK_CONNECTED;
                rg_network_t info = rg_network_get_info();
                RG_LOGI("Access point started! IP: %s", info.ip_addr);
                rg_system_event(RG_EVENT_NETWORK_CONNECTED, NULL);
            }
        }
    }
    else if (event_base == IP_EVENT)
    {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            wifi_retry_count = 0;
            esp_wifi_set_ps(WIFI_PS_NONE);
            #if defined(RG_GAMEPAD_USE_ESPNOW)
            uint8_t primary = 0;
            wifi_second_chan_t second;
            if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary > 0)
            {
                rg_input_espnow_notify_channel_switch(primary);
            }
            #endif
            if (wifi_user_started)
            {
                network_state = RG_NETWORK_CONNECTED;
                rg_network_t info = rg_network_get_info();
                RG_LOGI("Connected! IP: %s, Chan: %d, RSSI: %d", info.ip_addr, info.channel, info.rssi);
                #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
                if (esp_sntp_enabled())
                    esp_sntp_restart();
                else
                    esp_sntp_init();
                #else
                esp_sntp_stop();
                esp_sntp_init();
                #endif
                rg_system_event(RG_EVENT_NETWORK_CONNECTED, NULL);
            }
        }
        else if (event_id == IP_EVENT_AP_STAIPASSIGNED)
        {
            ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
            RG_LOGI("Access point assigned IP to client: "IPSTR, IP2STR(&event->ip));
        }
    }

    RG_LOGV("Event: %d %d\n", (int)event_base, (int)event_id);
}
#endif

#define CFG_KEY(key, slot) ({snprintf(key_buffer, sizeof(key_buffer), "%s%d", key, slot); key_buffer;})

bool rg_network_wifi_read_config(int slot, rg_wifi_config_t *out)
{
    if (slot < 0 || slot > 99)
        return false;

    rg_wifi_config_t config = {0};
    char key_buffer[64] = {0};
    char *ptr;

    RG_LOGD("Looking for '%s' (slot %d)", CFG_KEY(SETTING_WIFI_SSID, slot), slot);

    if ((ptr = rg_settings_get_string(NS_WIFI, CFG_KEY(SETTING_WIFI_SSID, slot), NULL)))
        memccpy(config.ssid, ptr, 0, 32), free(ptr);
    if ((ptr = rg_settings_get_string(NS_WIFI, CFG_KEY(SETTING_WIFI_PASSWORD, slot), NULL)))
        memccpy(config.password, ptr, 0, 64), free(ptr);
    config.channel = rg_settings_get_number(NS_WIFI, CFG_KEY(SETTING_WIFI_CHANNEL, slot), 0);
    config.ap_mode = rg_settings_get_number(NS_WIFI, CFG_KEY(SETTING_WIFI_MODE, slot), 0);

    if (!config.ssid[0] && slot == 0)
    {
        RG_LOGD("Looking for '%s' (slot %d)", SETTING_WIFI_SSID, slot);
        if ((ptr = rg_settings_get_string(NS_WIFI, SETTING_WIFI_SSID, NULL)))
            memccpy(config.ssid, ptr, 0, 32), free(ptr);
        if ((ptr = rg_settings_get_string(NS_WIFI, SETTING_WIFI_PASSWORD, NULL)))
            memccpy(config.password, ptr, 0, 64), free(ptr);
        config.channel = rg_settings_get_number(NS_WIFI, SETTING_WIFI_CHANNEL, 0);
        config.ap_mode = rg_settings_get_number(NS_WIFI, SETTING_WIFI_MODE, 0);
    }

    if (!config.ssid[0])
        return false;

    *out = config;
    return true;
}

bool rg_network_wifi_write_config(int slot, const rg_wifi_config_t *config)
{
    if (slot < 0 || slot > 99 || !config)
        return false;

    RG_LOGD("Writing config to slot %d", slot);

    char key_buffer[64] = {0};
    rg_settings_set_string(NS_WIFI, CFG_KEY(SETTING_WIFI_SSID, slot), config->ssid);
    rg_settings_set_string(NS_WIFI, CFG_KEY(SETTING_WIFI_PASSWORD, slot), config->password);
    rg_settings_set_number(NS_WIFI, CFG_KEY(SETTING_WIFI_CHANNEL, slot), config->channel);
    rg_settings_set_number(NS_WIFI, CFG_KEY(SETTING_WIFI_MODE, slot), config->ap_mode);

    return true;
}

bool rg_network_wifi_delete_config(int slot)
{
    if (slot < 0 || slot > 99)
        return false;

    RG_LOGD("Deleting config in slot %d", slot);

    char key_buffer[64] = {0};
    rg_settings_delete(NS_WIFI, CFG_KEY(SETTING_WIFI_SSID, slot));
    rg_settings_delete(NS_WIFI, CFG_KEY(SETTING_WIFI_PASSWORD, slot));
    rg_settings_delete(NS_WIFI, CFG_KEY(SETTING_WIFI_CHANNEL, slot));
    rg_settings_delete(NS_WIFI, CFG_KEY(SETTING_WIFI_MODE, slot));

    return true;
}

bool rg_network_wifi_set_config(const rg_wifi_config_t *config)
{
#ifdef RG_ENABLE_NETWORKING
    if (config)
        memcpy(&wifi_config, config, sizeof(wifi_config));
    else
        memset(&wifi_config, 0, sizeof(wifi_config));
    return true;
#else
    return false;
#endif
}

bool rg_network_wifi_start(void)
{
#ifdef RG_ENABLE_NETWORKING
    if (network_state <= RG_NETWORK_DISABLED)
    {
        if (!rg_network_init())
            return false;
    }
    wifi_config_t config = {0};
    esp_err_t err;

    if (!wifi_config.ssid[0])
    {
        RG_LOGW("Can't start wifi: No SSID has been configured.\n");
        return false;
    }

    wifi_user_started = true;

    if (wifi_config.ap_mode)
    {
        netif = netif_ap;
        memcpy(config.ap.ssid, wifi_config.ssid, 32);
        memcpy(config.ap.password, wifi_config.password, 64);
        config.ap.authmode = wifi_config.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        config.ap.channel = wifi_config.channel ?: 1;
        config.ap.max_connection = 1;
        #if defined(RG_GAMEPAD_USE_ESPNOW)
        TRY(esp_wifi_set_mode(WIFI_MODE_APSTA));
        #else
        TRY(esp_wifi_set_mode(WIFI_MODE_AP));
        #endif
        TRY(esp_wifi_set_config(WIFI_IF_AP, &config));
        err = esp_wifi_start();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            RG_LOGE("esp_wifi_start = 0x%x\n", err);
            goto fail;
        }
        esp_wifi_set_ps(WIFI_PS_NONE);
        #if defined(RG_GAMEPAD_USE_ESPNOW)
        if (config.ap.channel > 0)
        {
            rg_input_espnow_notify_channel_switch(config.ap.channel);
        }
        #endif
    }
    else
    {
        netif = netif_sta;
        memcpy(config.sta.ssid, wifi_config.ssid, 32);
        memcpy(config.sta.password, wifi_config.password, 64);
        config.sta.channel = wifi_config.channel;
        TRY(esp_wifi_set_mode(WIFI_MODE_STA));
        TRY(esp_wifi_set_config(WIFI_IF_STA, &config));
        network_state = RG_NETWORK_CONNECTING;
        wifi_retry_count = 0;
        err = esp_wifi_start();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            RG_LOGE("esp_wifi_start = 0x%x\n", err);
            goto fail;
        }
        esp_wifi_set_ps(WIFI_PS_NONE);
        RG_LOGI("Connecting to '%s' (channel: %d)...", wifi_config.ssid, wifi_config.channel);
        esp_wifi_connect();
    }
    return true;
fail:
    wifi_user_started = false;
#endif
    return false;
}

void rg_network_wifi_stop(void)
{
#ifdef RG_ENABLE_NETWORKING
    wifi_user_started = false;
    if (network_state <= RG_NETWORK_DISABLED)
        return;
    network_state = RG_NETWORK_DISCONNECTED;
    #if defined(RG_GAMEPAD_USE_ESPNOW)
    // Proactively notify gamepad to return to channel 1 before console switches channel
    rg_input_espnow_notify_channel_switch(1);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    #else
    esp_wifi_stop();
    #endif
    memset(&wifi_config, 0, sizeof(wifi_config));
    netif = NULL;
    rg_system_event(RG_EVENT_NETWORK_DISCONNECTED, NULL);
    RG_LOGI("Wifi stopped.");
#endif
}

rg_network_t rg_network_get_info(void)
{
    rg_network_t info = {0};
#ifdef RG_ENABLE_NETWORKING
    if (wifi_user_started && netif)
    {
        memcpy(info.name, wifi_config.ssid, 32);
        info.channel = wifi_config.channel;

        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
        {
            snprintf(info.ip_addr, 16, IPSTR, IP2STR(&ip_info.ip));
        }

        if (network_state == RG_NETWORK_CONNECTED)
        {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
            {
                info.channel = ap_info.primary;
                info.rssi = ap_info.rssi;
            }
        }
    }
    info.state = wifi_user_started ? network_state : RG_NETWORK_DISCONNECTED;
#endif
    return info;
}

bool rg_network_init(void)
{
#ifdef RG_ENABLE_NETWORKING
    if (network_state > RG_NETWORK_DISABLED)
        return true;

    // Init event loop first
    esp_err_t err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        RG_LOGE("esp_event_loop_create_default = 0x%x\n", err);
        goto fail;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        RG_LOGE("esp_event_handler_register WIFI_EVENT = 0x%x\n", err);
        goto fail;
    }

    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        RG_LOGE("esp_event_handler_register IP_EVENT = 0x%x\n", err);
        goto fail;
    }

    // Then TCP stack
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        RG_LOGE("esp_netif_init = 0x%x\n", err);
        goto fail;
    }

    if (!netif_sta)
    {
        netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif_sta)
            netif_sta = esp_netif_create_default_wifi_sta();
    }
    if (!netif_ap)
    {
        netif_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (!netif_ap)
            netif_ap = esp_netif_create_default_wifi_ap();
    }

    if (netif_sta)
        esp_netif_set_hostname(netif_sta, RG_TARGET_NAME);
    if (netif_ap)
        esp_netif_set_hostname(netif_ap, RG_TARGET_NAME);

    // Wifi may use nvs for calibration data
    if (nvs_flash_init() != ESP_OK && nvs_flash_erase() == ESP_OK)
        nvs_flash_init();

    // Initialize wifi driver (it won't enable the radio yet)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        RG_LOGE("esp_wifi_init = 0x%x\n", err);
        goto fail;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        RG_LOGE("esp_wifi_set_storage = 0x%x\n", err);
        goto fail;
    }

    // Setup SNTP client but don't query it yet
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");

    network_state = RG_NETWORK_DISCONNECTED;
    wifi_user_started = false;

    return true;
fail:
    network_state = RG_NETWORK_DISABLED;
    wifi_user_started = false;
#endif
    return false;
}

void rg_network_deinit(void)
{
#ifdef RG_ENABLE_NETWORKING
    rg_network_wifi_stop();
    network_state = RG_NETWORK_DISABLED;
    wifi_user_started = false;
    // We can't really deinit event loops and tcpip adapters...
#endif
}

rg_http_req_t *rg_network_http_open(const char *url, const rg_http_cfg_t *cfg)
{
    RG_ASSERT_ARG(url != NULL);
#ifdef RG_ENABLE_NETWORKING
    rg_http_req_t *req = calloc(1, sizeof(rg_http_req_t));
    if (!req)
    {
        RG_LOGE("Out of memory");
        return NULL;
    }
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };
    if (cfg)
    {
        // TODO: Handle more options
        if (cfg->post_data)
            http_cfg.method = HTTP_METHOD_POST;
    }
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client)
    {
        RG_LOGE("Failed to init http client");
        free(req);
        return NULL;
    }
    if (cfg && cfg->post_data)
    {
        esp_http_client_set_post_field(client, cfg->post_data, strlen(cfg->post_data));
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        RG_LOGE("Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(req);
        return NULL;
    }
    req->client = client;
    req->status_code = esp_http_client_fetch_headers(client);
    req->content_length = esp_http_client_get_content_length(client);
    return req;
#else
    return NULL;
#endif
}

int rg_network_http_read(rg_http_req_t *req, void *buffer, size_t length)
{
    RG_ASSERT_ARG(req != NULL);
#ifdef RG_ENABLE_NETWORKING
    return esp_http_client_read(req->client, buffer, length);
#else
    return -1;
#endif
}

void rg_network_http_close(rg_http_req_t *req)
{
#ifdef RG_ENABLE_NETWORKING
    if (req)
    {
        esp_http_client_close(req->client);
        esp_http_client_cleanup(req->client);
        free(req);
    }
#endif
}
