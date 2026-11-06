// SPDX-License-Identifier: Apache-2.0
#include "app_wifi.h"

#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "mosaico_shared.h"
#include "nvs.h"

enum {
    EVENT_DISCONNECTED = BIT0,
    EVENT_IP = BIT1,
    EVENT_SCAN_DONE = BIT2,
    EVENT_WAKE = BIT3,
    PROFILE_COUNT = 4,
    PROFILES_VERSION = 1,
    SWITCH_MS = 250,
    CONNECT_MS = 12000,
    RETRY_MS = 3000,
    SCAN_MS = 8000,
    WORKER_MS = 100,
    WORKER_STACK_BYTES = 6144,
};

typedef enum { OFFLINE, SWITCHING, JOINING, ONLINE } wifi_phase_t;

typedef struct {
    char ssid[APP_WIFI_SSID_BYTES];
    char password[APP_WIFI_PASSWORD_BYTES];
} credential_t;

typedef struct {
    uint8_t version;
    uint8_t count;
    credential_t entries[PROFILE_COUNT];
} profile_list_t;

/* Worker owns every radio call, the profile store, and active credentials.
 * UI submissions and event callbacks update only the guarded mailbox/state. */
struct app_wifi {
    TaskHandle_t worker;
    esp_netif_t *netif;
    esp_event_handler_instance_t wifi_event;
    esp_event_handler_instance_t ip_event;
    app_wifi_snapshot_t snapshot;
    uint32_t generation;
    int disconnect_reason;
    bool scan_requested;
    bool connect_requested;
    bool use_saved;
    credential_t request;
    wifi_phase_t phase;
    bool manual;
    bool restore;
    unsigned auto_index;
    int64_t deadline;
    int64_t scan_deadline;
    profile_list_t profiles;
    profile_list_t staged;
    credential_t fallback;
    credential_t last_working;
    credential_t candidate;
    wifi_config_t config;
    wifi_ap_record_t records[APP_WIFI_MAX_APS];
};

static const char *TAG = "pet_wifi";
static const char *NAMESPACE = "pet_wifi";
static struct app_wifi station;
static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool valid_password(const char *password)
{
    size_t n = strnlen(password, APP_WIFI_PASSWORD_BYTES);
    if (n == 0 || (n >= 8 && n < 64)) return true;
    return n == 64 && strspn(password, "0123456789abcdefABCDEF") == n;
}

static bool valid_credential(const credential_t *credential)
{
    return credential->ssid[0] && memchr(credential->ssid, 0, APP_WIFI_SSID_BYTES) &&
           memchr(credential->password, 0, APP_WIFI_PASSWORD_BYTES) &&
           valid_password(credential->password);
}

static void load_profiles(app_wifi_handle_t wifi)
{
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(wifi->profiles);
        esp_err_t err = nvs_get_blob(handle, "networks", &wifi->profiles, &size);
        nvs_close(handle);
        bool valid = err == ESP_OK && size == sizeof(wifi->profiles) &&
                     wifi->profiles.version == PROFILES_VERSION && wifi->profiles.count <= PROFILE_COUNT;
        for (unsigned i = 0; valid && i < wifi->profiles.count; ++i) {
            valid = valid_credential(&wifi->profiles.entries[i]);
        }
        if (!valid) mbedtls_platform_zeroize(&wifi->profiles, sizeof(wifi->profiles));
    }
    if (mosaico_shared_get("wifi_ssid", wifi->fallback.ssid, sizeof(wifi->fallback.ssid)) != ESP_OK) {
        wifi->fallback.ssid[0] = '\0';
        return;
    }
    esp_err_t err = mosaico_shared_get("wifi_password", wifi->fallback.password, sizeof(wifi->fallback.password));
    if ((err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) || !valid_credential(&wifi->fallback)) {
        mbedtls_platform_zeroize(&wifi->fallback, sizeof(wifi->fallback));
    }
}

static const credential_t *saved_credential(app_wifi_handle_t wifi, const char *ssid)
{
    for (unsigned i = 0; i < wifi->profiles.count; ++i) {
        if (!strcmp(wifi->profiles.entries[i].ssid, ssid)) return &wifi->profiles.entries[i];
    }
    if (wifi->fallback.ssid[0] && !strcmp(wifi->fallback.ssid, ssid)) return &wifi->fallback;
    return NULL;
}

static esp_err_t save_profile(app_wifi_handle_t wifi)
{
    profile_list_t *next = &wifi->staged;
    memset(next, 0, sizeof(*next));
    next->version = PROFILES_VERSION;
    next->entries[next->count++] = wifi->candidate;
    for (unsigned i = 0; i < wifi->profiles.count && next->count < PROFILE_COUNT; ++i) {
        const credential_t *existing = &wifi->profiles.entries[i];
        if (strcmp(existing->ssid, wifi->candidate.ssid)) next->entries[next->count++] = *existing;
    }
    esp_err_t err = ESP_OK;
    if (memcmp(next, &wifi->profiles, sizeof(*next))) {
        nvs_handle_t handle;
        err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_blob(handle, "networks", next, sizeof(*next));
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err == ESP_OK) wifi->profiles = *next;
    }
    mbedtls_platform_zeroize(next, sizeof(*next));
    return err;
}

void app_wifi_read(app_wifi_handle_t wifi, app_wifi_snapshot_t *out)
{
    if (!wifi) {
        memset(out, 0, sizeof(*out));
        return;
    }
    portENTER_CRITICAL(&guard);
    *out = wifi->snapshot;
    portEXIT_CRITICAL(&guard);
}

bool app_wifi_link(app_wifi_handle_t wifi, uint32_t *generation)
{
    portENTER_CRITICAL(&guard);
    bool online = wifi && wifi->snapshot.online;
    if (generation) *generation = wifi ? wifi->generation : 0;
    portEXIT_CRITICAL(&guard);
    return online;
}

static void publish_link(app_wifi_handle_t wifi, const char *ssid, bool online, bool connecting,
                         app_wifi_result_t result)
{
    portENTER_CRITICAL(&guard);
    app_wifi_snapshot_t *state = &wifi->snapshot;
    if (state->online != online) ++wifi->generation;
    state->online = online;
    state->connecting = connecting;
    strlcpy(state->ssid, ssid, sizeof(state->ssid));
    if (result != APP_WIFI_RESULT_NONE) state->result = result;
    ++state->revision;
    portEXIT_CRITICAL(&guard);
}

static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    app_wifi_handle_t wifi = arg;
    if (!wifi->worker) return;  // No association is started before the worker exists.
    unsigned flags = 0;
    if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) flags = EVENT_IP;
        if (id == IP_EVENT_STA_LOST_IP) flags = EVENT_WAKE;
    } else if (id == WIFI_EVENT_STA_DISCONNECTED && data) {
        const wifi_event_sta_disconnected_t *lost = data;
        portENTER_CRITICAL(&guard);
        wifi->disconnect_reason = lost->reason;
        portEXIT_CRITICAL(&guard);
        flags = EVENT_DISCONNECTED;
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        flags = EVENT_SCAN_DONE;
    }
    if (flags) xTaskNotify(wifi->worker, flags, eSetBits);
}

esp_err_t app_wifi_scan(app_wifi_handle_t wifi)
{
    if (!wifi) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&guard);
    if (!wifi->snapshot.scanning) {
        wifi->scan_requested = true;
        wifi->snapshot.scanning = true;
        wifi->snapshot.scan_failed = false;
        ++wifi->snapshot.revision;
    }
    portEXIT_CRITICAL(&guard);
    xTaskNotify(wifi->worker, EVENT_WAKE, eSetBits);
    return ESP_OK;
}

esp_err_t app_wifi_connect(app_wifi_handle_t wifi, const char *ssid, const char *password)
{
    if (!wifi) return ESP_ERR_INVALID_STATE;
    if (!ssid || !ssid[0] || strnlen(ssid, APP_WIFI_SSID_BYTES) >= APP_WIFI_SSID_BYTES ||
        (password && !valid_password(password))) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&guard);
    if (wifi->snapshot.result == APP_WIFI_RESULT_PENDING) {
        portEXIT_CRITICAL(&guard);
        return ESP_ERR_INVALID_STATE;
    }
    strlcpy(wifi->request.ssid, ssid, sizeof(wifi->request.ssid));
    strlcpy(wifi->request.password, password ? password : "", sizeof(wifi->request.password));
    wifi->connect_requested = true;
    wifi->use_saved = password == NULL;
    wifi->snapshot.result = APP_WIFI_RESULT_PENDING;
    ++wifi->snapshot.revision;
    portEXIT_CRITICAL(&guard);
    xTaskNotify(wifi->worker, EVENT_WAKE, eSetBits);
    return ESP_OK;
}

static void end_scan(app_wifi_handle_t wifi, esp_err_t err)
{
    uint16_t count = APP_WIFI_MAX_APS;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, wifi->records);
    if (err != ESP_OK) {
        esp_wifi_clear_ap_list();
        count = 0;
    }
    portENTER_CRITICAL(&guard);
    app_wifi_snapshot_t *state = &wifi->snapshot;
    state->count = 0;
    for (unsigned i = 0; i < count; ++i) {
        const wifi_ap_record_t *record = &wifi->records[i];
        const char *ssid = (const char *)record->ssid;
        if (!*ssid || (state->online && !strcmp(ssid, state->ssid))) continue;
        bool duplicate = false;
        for (unsigned j = 0; j < state->count; ++j) {
            if (!strcmp(ssid, state->aps[j].ssid)) duplicate = true;
        }
        if (duplicate) continue;
        app_wifi_ap_t *ap = &state->aps[state->count++];
        strlcpy(ap->ssid, ssid, sizeof(ap->ssid));
        ap->rssi = record->rssi;
        ap->secured = record->authmode != WIFI_AUTH_OPEN;
        const credential_t *saved = saved_credential(wifi, ap->ssid);
        ap->saved = saved && (ap->secured == (saved->password[0] != '\0'));
    }
    state->scanning = false;
    state->scan_failed = err != ESP_OK;
    ++state->revision;
    portEXIT_CRITICAL(&guard);
    wifi->scan_deadline = 0;
}

static void attempt_failed(app_wifi_handle_t wifi, app_wifi_result_t reason)
{
    if (wifi->manual) wifi->restore = wifi->last_working.ssid[0] != '\0';
    publish_link(wifi, "", false, false, wifi->manual ? reason : APP_WIFI_RESULT_NONE);
    mbedtls_platform_zeroize(&wifi->candidate, sizeof(wifi->candidate));
    wifi->phase = OFFLINE;
    wifi->deadline = now_ms() + (wifi->manual ? SWITCH_MS : RETRY_MS);
    wifi->manual = false;
    esp_wifi_disconnect();
}

static void start_switch(app_wifi_handle_t wifi, bool manual)
{
    if (wifi->scan_deadline) {
        esp_wifi_scan_stop();
        end_scan(wifi, ESP_ERR_INVALID_STATE);
    }
    wifi->manual = manual;
    wifi->phase = SWITCHING;
    wifi->deadline = now_ms() + SWITCH_MS;
    publish_link(wifi, wifi->candidate.ssid, false, true, APP_WIFI_RESULT_NONE);
    esp_wifi_disconnect();
}

static void join(app_wifi_handle_t wifi)
{
    wifi_config_t *config = &wifi->config;
    memcpy(config->sta.ssid, wifi->candidate.ssid, strlen(wifi->candidate.ssid));
    memcpy(config->sta.password, wifi->candidate.password, strlen(wifi->candidate.password));
    config->sta.threshold.authmode = wifi->candidate.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, config);
    mbedtls_platform_zeroize(config, sizeof(*config));
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Station connection request: %s", esp_err_to_name(err));
        attempt_failed(wifi, APP_WIFI_RESULT_UNREACHABLE);
        return;
    }
    wifi->phase = JOINING;
    wifi->deadline = now_ms() + CONNECT_MS;
}

static bool has_ip(app_wifi_handle_t wifi, const char *ssid)
{
    esp_netif_ip_info_t ip;
    return esp_wifi_sta_get_ap_info(&wifi->records[0]) == ESP_OK &&
           !strcmp((const char *)wifi->records[0].ssid, ssid) &&
           esp_netif_get_ip_info(wifi->netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static void connected(app_wifi_handle_t wifi)
{
    wifi->last_working = wifi->candidate;
    app_wifi_result_t result = APP_WIFI_RESULT_NONE;
    if (wifi->manual) {
        result = save_profile(wifi) == ESP_OK ? APP_WIFI_RESULT_CONNECTED : APP_WIFI_RESULT_SAVE_FAILED;
    }
    wifi->manual = false;
    wifi->restore = false;
    wifi->auto_index = 0;
    wifi->phase = ONLINE;
    publish_link(wifi, wifi->last_working.ssid, true, false, result);
    mbedtls_platform_zeroize(&wifi->candidate, sizeof(wifi->candidate));
    ESP_LOGI(TAG, "Station has IP");
}

static bool start_user_request(app_wifi_handle_t wifi)
{
    portENTER_CRITICAL(&guard);
    bool requested = wifi->connect_requested;
    bool use_saved = wifi->use_saved;
    if (requested) {
        wifi->candidate = wifi->request;
        mbedtls_platform_zeroize(&wifi->request, sizeof(wifi->request));
        wifi->connect_requested = false;
    }
    portEXIT_CRITICAL(&guard);
    if (!requested) return false;
    if (use_saved) {
        const credential_t *saved = saved_credential(wifi, wifi->candidate.ssid);
        if (!saved) {
            portENTER_CRITICAL(&guard);
            wifi->snapshot.result = APP_WIFI_RESULT_UNREACHABLE;
            ++wifi->snapshot.revision;
            portEXIT_CRITICAL(&guard);
            return false;
        }
        wifi->candidate = *saved;
    }
    start_switch(wifi, true);
    return true;
}

static void auto_connect(app_wifi_handle_t wifi)
{
    const credential_t *next = NULL;
    const unsigned total = wifi->profiles.count + (wifi->fallback.ssid[0] != '\0');
    if (wifi->restore) {
        wifi->restore = false;
        next = &wifi->last_working;
    } else if (total) {
        unsigned index = wifi->auto_index++ % total;
        next = index < wifi->profiles.count ? &wifi->profiles.entries[index] : &wifi->fallback;
    }
    if (next && next->ssid[0]) {
        wifi->candidate = *next;
        start_switch(wifi, false);
    } else {
        wifi->deadline = now_ms() + RETRY_MS;
    }
}

static bool start_scan(app_wifi_handle_t wifi)
{
    portENTER_CRITICAL(&guard);
    bool requested = wifi->scan_requested;
    wifi->scan_requested = false;
    portEXIT_CRITICAL(&guard);
    if (!requested) return false;
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err == ESP_OK) wifi->scan_deadline = now_ms() + SCAN_MS;
    else end_scan(wifi, err);
    return true;
}

static void worker(void *arg)
{
    app_wifi_handle_t wifi = arg;
    load_profiles(wifi);
    for (;;) {
        uint32_t flags = 0;
        xTaskNotifyWait(0, UINT32_MAX, &flags, pdMS_TO_TICKS(WORKER_MS));
        const int64_t now = now_ms();
        if (start_user_request(wifi)) continue;
        if (wifi->scan_deadline && (flags & EVENT_SCAN_DONE)) end_scan(wifi, ESP_OK);
        if (wifi->scan_deadline && now >= wifi->scan_deadline) {
            esp_wifi_scan_stop();
            end_scan(wifi, ESP_ERR_TIMEOUT);
        }
        if (wifi->phase == SWITCHING && now >= wifi->deadline) join(wifi);
        if (wifi->phase == JOINING) {
            if ((flags & EVENT_IP) && has_ip(wifi, wifi->candidate.ssid)) connected(wifi);
            else if (now >= wifi->deadline) attempt_failed(wifi, APP_WIFI_RESULT_UNREACHABLE);
            else if (flags & EVENT_DISCONNECTED) {
                portENTER_CRITICAL(&guard);
                int reason = wifi->disconnect_reason;
                portEXIT_CRITICAL(&guard);
                if (reason != WIFI_REASON_ASSOC_LEAVE) {
                    bool auth = reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
                    attempt_failed(wifi, auth ? APP_WIFI_RESULT_AUTH : APP_WIFI_RESULT_UNREACHABLE);
                }
            }
        }
        if (wifi->phase == ONLINE && (flags & (EVENT_DISCONNECTED | EVENT_WAKE)) &&
            !has_ip(wifi, wifi->last_working.ssid)) {
            publish_link(wifi, "", false, false, APP_WIFI_RESULT_NONE);
            wifi->phase = OFFLINE;
            wifi->restore = true;
            wifi->deadline = now + RETRY_MS;
        }
        if (wifi->phase == ONLINE || wifi->phase == OFFLINE) {
            if (!wifi->scan_deadline && start_scan(wifi)) continue;
            if (wifi->phase == OFFLINE && !wifi->scan_deadline && now >= wifi->deadline) auto_connect(wifi);
        }
    }
}

esp_err_t app_wifi_start(app_wifi_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    app_wifi_handle_t wifi = &station;
    if (wifi->worker) return ESP_ERR_INVALID_STATE;
    bool initialized = false, running = false;
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) return ret;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    wifi->netif = esp_netif_create_default_wifi_sta();
    ESP_GOTO_ON_FALSE(wifi->netif, ESP_ERR_NO_MEM, cleanup, TAG, "create Wi-Fi station");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_GOTO_ON_ERROR(esp_wifi_init(&init), cleanup, TAG, "initialize Wi-Fi");
    initialized = true;
    ESP_GOTO_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), cleanup, TAG, "select RAM storage");
    ESP_GOTO_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), cleanup, TAG, "select station mode");
    ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, wifi,
                      &wifi->wifi_event), cleanup, TAG, "watch Wi-Fi");
    ESP_GOTO_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, event, wifi,
                      &wifi->ip_event), cleanup, TAG, "watch IP");
    ESP_GOTO_ON_ERROR(esp_wifi_start(), cleanup, TAG, "start station");
    running = true;
    ESP_GOTO_ON_FALSE(xTaskCreate(worker, "pet_wifi", WORKER_STACK_BYTES, wifi, 5, &wifi->worker) == pdPASS,
                      ESP_ERR_NO_MEM, cleanup, TAG, "start Wi-Fi worker");
    *out = wifi;
    return ESP_OK;

cleanup:
    if (wifi->ip_event) esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, wifi->ip_event);
    if (wifi->wifi_event) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi->wifi_event);
    if (running) esp_wifi_stop();
    if (initialized) esp_wifi_deinit();
    if (wifi->netif) esp_netif_destroy_default_wifi(wifi->netif);
    memset(wifi, 0, sizeof(*wifi));
    return ret;
}
