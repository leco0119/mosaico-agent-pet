// SPDX-License-Identifier: Apache-2.0
#include "status_link.h"

#include <stdio.h>
#include <string.h>
#include "esp_iris.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pet_settings.h"

#define PET_SERVICE 0x5354u
#define PET_SET 0x0001u
#define PET_STATUS 0x0002u
#define PET_PAYLOAD_BYTES 512u
#define PET_DISPATCH_MS 50u

/* One application-lifetime transport. Iris workers own the pending mailbox;
 * the GSP timer owns the controller/inflight buffer. lock protects mailbox
 * and the published diagnostic snapshot. Latest pending status wins. */
static struct {
    app_pet_handle_t pet;
    app_voice_handle_t voice;
    app_wifi_handle_t wifi;
    pet_settings_handle_t settings;
    app_wifi_snapshot_t network;
    uint32_t voice_revision;
    SemaphoreHandle_t lock;
    void *timer;
    size_t pending_size;
    char pending[PET_PAYLOAD_BYTES];
    char inflight[PET_PAYLOAD_BYTES];
    app_pet_snapshot_t snapshot;
} link;

static bool wifi_scan(void *ctx)
{
    (void)ctx;
    return app_wifi_scan(link.wifi) == ESP_OK;
}

static bool wifi_connect(void *ctx, const char *ssid, const char *password)
{
    (void)ctx;
    return app_wifi_connect(link.wifi, ssid, password) == ESP_OK;
}

static void volume(void *ctx, unsigned percent, bool persist)
{
    (void)ctx;
    app_voice_set_volume(link.voice, percent, persist);
}

static void preview(void *ctx)
{
    (void)ctx;
    app_voice_preview(link.voice);
}

static void check_binding(void *ctx)
{
    (void)ctx;
    app_voice_check_binding(link.voice);
}

static void navigation(void *ctx, bool open)
{
    (void)ctx;
    app_voice_settings_open(link.voice, open);
}

static void render_voice(const app_voice_snapshot_t *voice)
{
    switch (voice->state) {
        case APP_VOICE_IDLE:
        case APP_VOICE_BINDING:
            app_pet_show_voice(link.pet, NULL, NULL, 0, voice->ready);
            break;
        case APP_VOICE_NO_WIFI:
            app_pet_show_voice(link.pet, "请先配网", "左上角 Wi-Fi", 2, false);
            break;
        case APP_VOICE_CONNECTING:
            app_pet_show_voice(link.pet, "连接中", "小智官方云", 1, voice->ready);
            break;
        case APP_VOICE_LISTENING:
            app_pet_show_voice(link.pet, "正在听", "松开 AI 键发送", 2, true);
            break;
        case APP_VOICE_THINKING:
            app_pet_show_voice(link.pet, "思考中", "", 1, true);
            break;
        case APP_VOICE_SPEAKING:
            app_pet_show_voice(link.pet, "正在说话", "", 1, true);
            break;
        case APP_VOICE_ERROR:
            app_pet_show_voice(link.pet, "语音连接失败", "请重试", 2, voice->ready);
            break;
    }
}

static void dispatch(esp_gsp_handle_t ui, void *context)
{
    (void)ui;
    (void)context;
    if (xSemaphoreTake(link.lock, 0) != pdTRUE) return;
    size_t size = link.pending_size;
    if (size) memcpy(link.inflight, link.pending, size);
    link.pending_size = 0;
    xSemaphoreGive(link.lock);
    app_voice_snapshot_t voice;
    app_voice_read(link.voice, &voice);
    app_wifi_read(link.wifi, &link.network);
    pet_settings_update(link.settings, &link.network, &voice);
    if (voice.revision != link.voice_revision) {
        render_voice(&voice);
        link.voice_revision = voice.revision;
    }
    if (size) {
        unsigned applied = 0;
        app_pet_apply(link.pet, link.inflight, size, &applied);
    }
    app_pet_snapshot_t snapshot;
    if (app_pet_read(link.pet, &snapshot) == ESP_GSP_OK &&
        xSemaphoreTake(link.lock, 0) == pdTRUE) {
        link.snapshot = snapshot;
        xSemaphoreGive(link.lock);
    }
}

static esp_err_t set_handler(const esp_iris_rpc_request_t *req, uint8_t *response,
                             size_t capacity, size_t *size, void *context)
{
    (void)context;
    static const char accepted[] = "{\"ok\":true,\"queued\":true}";
    if (!req || !req->payload || !req->payload_size || !response || !size) return ESP_ERR_INVALID_ARG;
    if (req->payload_size > PET_PAYLOAD_BYTES || capacity < sizeof(accepted) - 1) return ESP_ERR_INVALID_SIZE;
    if (xSemaphoreTake(link.lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memcpy(link.pending, req->payload, req->payload_size);
    link.pending_size = req->payload_size;
    xSemaphoreGive(link.lock);
    memcpy(response, accepted, sizeof(accepted) - 1);
    *size = sizeof(accepted) - 1;
    return ESP_OK;
}

static esp_err_t status_handler(const esp_iris_rpc_request_t *req, uint8_t *response,
                                size_t capacity, size_t *size, void *context)
{
    (void)req;
    (void)context;
    if (!response || !size) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link.lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    app_pet_snapshot_t snapshot = link.snapshot;
    xSemaphoreGive(link.lock);
    app_voice_snapshot_t voice;
    app_voice_read(link.voice, &voice);
    int n = snprintf((char *)response, capacity,
                     "{\"anim\":%u,\"connected\":%s,\"updates\":%lu,\"last_error\":%d,"
                     "\"voice_state\":%d,\"voice_ready\":%s,\"voice_turns\":%lu,"
                     "\"wifi_online\":%s,\"volume\":%u}",
                     snapshot.animation, snapshot.connected ? "true" : "false",
                     (unsigned long)snapshot.updates, (int)snapshot.last_error,
                     voice.state, voice.ready ? "true" : "false", (unsigned long)voice.completed_turns,
                     app_wifi_link(link.wifi, NULL) ? "true" : "false", (unsigned)voice.volume);
    if (n < 0 || (size_t)n >= capacity) return ESP_ERR_INVALID_SIZE;
    *size = (size_t)n;
    return ESP_OK;
}

esp_err_t status_link_start(app_pet_handle_t pet, esp_gsp_handle_t ui, app_voice_handle_t voice,
                            app_wifi_handle_t wifi)
{
    if (!pet || !ui || link.pet) return ESP_ERR_INVALID_STATE;
    link.lock = xSemaphoreCreateMutex();
    if (!link.lock) return ESP_ERR_NO_MEM;
    link.pet = pet;
    link.voice = voice;
    link.wifi = wifi;
    static const pet_settings_ops_t ops = {
        .scan = wifi_scan, .connect = wifi_connect, .volume = volume, .preview = preview,
        .check_binding = check_binding, .navigation = navigation,
    };
    if (!link.settings) {
        esp_gsp_err_t settings_err = pet_settings_create(ui, &ops, NULL, &link.settings);
        if (settings_err != ESP_GSP_OK) ESP_LOGW("pet_link", "Settings UI: %d", (int)settings_err);
    }
    link.timer = esp_gsp_timer_create(ui, PET_DISPATCH_MS, dispatch, NULL);
    if (!link.timer) {
        link.pet = NULL;
        vSemaphoreDelete(link.lock);
        link.lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_iris_rpc_register(PET_SERVICE, PET_SET, set_handler, NULL);
    bool registered = err == ESP_OK;
    if (registered) err = esp_iris_rpc_register(PET_SERVICE, PET_STATUS, status_handler, NULL);
    if (err != ESP_OK) {
        if (registered && esp_iris_rpc_unregister(PET_SERVICE, PET_SET) != ESP_OK) return err;
        if (esp_gsp_timer_delete(ui, link.timer) != ESP_GSP_OK) return err;
        link.pet = NULL;
        vSemaphoreDelete(link.lock);
        link.lock = NULL;
    }
    return err;
}
