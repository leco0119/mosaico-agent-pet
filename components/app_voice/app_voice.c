// SPDX-License-Identifier: Apache-2.0
#include "app_voice.h"

#include <string.h>
#include "bsp/button.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_xiaozhi_chat.h"
#include "freertos/idf_additions.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "nvs.h"
#include "voice_audio.h"
#include "voice_cloud.h"

enum {
    BUTTON_HELD = BIT0,
    BUTTON_PRESSED = BIT1,
    BUTTON_RELEASED = BIT2,
    SETTINGS_OPEN = BIT3,
    CLOUD_ONLINE = BIT4,
    TURN_FAILED = BIT5,
    REPLY_ENABLED = BIT6,
    REPLY_END = BIT7,
    REPLY_OVERFLOW = BIT8,
    PREVIEW_REQUEST = BIT9,
    BIND_CHECK = BIT10,
};

enum {
    // The S31 fixed-point Opus encoder has large stack frames. The reference
    // uses 24 KiB for Opus alone; this worker also owns the call and I/O state.
    WORKER_STACK_BYTES = 28 * 1024,
    WORKER_PRIORITY = 5,
    // Cloud audio can arrive faster than the speaker drains it.
    REPLY_BUFFER_BYTES = 128 * 1024,
    WORKER_POLL_MS = 100,
    CLOUD_RETRY_MS = 10000,
    ACTIVATE_POLL_MS = 5000,
    CODE_REFRESH_MS = 60000,
    NOTICE_MS = 3000,
    MAX_HOLD_MS = 30000,
    REPLY_TIMEOUT_MS = 30000,
    REPLY_DRAIN_MS = 400,
};

static const char *TAG = "pet_voice";
static const char *AUDIO_SETTINGS = "pet_audio";

/* The worker owns chat, the codec, and all voice state transitions.
 * Callbacks only update event bits or copy one packet to the reply buffer. */
struct app_voice {
    EventGroupHandle_t events;
    RingbufHandle_t reply;
    button_handle_t button;
    app_wifi_handle_t wifi;
    uint32_t network_generation;
    voice_audio_t audio;
    esp_xiaozhi_chat_handle_t chat;
    esp_mcp_t *mcp;
    int64_t activation_refresh;
    bool activation_pending;
    bool started;
    bool save_volume;
    uint8_t volume_to_save;
    uint8_t saved_volume;
    app_voice_snapshot_t snapshot;
};

static struct app_voice service;
static portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void publish(app_voice_handle_t voice, app_voice_state_t state, bool ready, const char *code)
{
    if (!code) code = "";
    portENTER_CRITICAL(&snapshot_lock);
    app_voice_snapshot_t *snapshot = &voice->snapshot;
    if (snapshot->state != state || snapshot->ready != ready || strcmp(snapshot->binding_code, code)) {
        snapshot->state = state;
        snapshot->ready = ready;
        strcpy(snapshot->binding_code, code);
        ++snapshot->revision;
    }
    portEXIT_CRITICAL(&snapshot_lock);
}

void app_voice_read(app_voice_handle_t voice, app_voice_snapshot_t *out)
{
    portENTER_CRITICAL(&snapshot_lock);
    *out = voice ? voice->snapshot : (app_voice_snapshot_t){.volume = APP_VOICE_DEFAULT_VOLUME};
    portEXIT_CRITICAL(&snapshot_lock);
}

static unsigned current_volume(app_voice_handle_t voice)
{
    portENTER_CRITICAL(&snapshot_lock);
    unsigned volume = voice->snapshot.volume;
    portEXIT_CRITICAL(&snapshot_lock);
    return volume;
}

void app_voice_set_volume(app_voice_handle_t voice, unsigned volume, bool persist)
{
    if (!voice || volume > APP_VOICE_MAX_VOLUME) return;
    portENTER_CRITICAL(&snapshot_lock);
    if (voice->snapshot.volume != volume) {
        voice->snapshot.volume = volume;
        ++voice->snapshot.revision;
    }
    if (persist) {
        voice->volume_to_save = volume;
        voice->save_volume = true;
    }
    portEXIT_CRITICAL(&snapshot_lock);
}

static void save_volume(app_voice_handle_t voice)
{
    portENTER_CRITICAL(&snapshot_lock);
    bool pending = voice->save_volume;
    uint8_t volume = voice->volume_to_save;
    voice->save_volume = false;
    portEXIT_CRITICAL(&snapshot_lock);
    if (!pending || volume == voice->saved_volume) return;
    nvs_handle_t settings;
    esp_err_t err = nvs_open(AUDIO_SETTINGS, NVS_READWRITE, &settings);
    if (err == ESP_OK) {
        err = nvs_set_u8(settings, "volume", volume);
        if (err == ESP_OK) err = nvs_commit(settings);
        nvs_close(settings);
    }
    if (err == ESP_OK) {
        voice->saved_volume = volume;
        ESP_LOGI(TAG, "Speaker volume saved: %u%%", volume);
    } else {
        ESP_LOGW(TAG, "Save speaker volume: %s", esp_err_to_name(err));
    }
}

void app_voice_preview(app_voice_handle_t voice)
{
    if (!voice) return;
    app_voice_snapshot_t state;
    app_voice_read(voice, &state);
    if (state.state == APP_VOICE_IDLE || state.state == APP_VOICE_BINDING || state.state == APP_VOICE_ERROR ||
        state.state == APP_VOICE_NO_WIFI) {
        xEventGroupSetBits(voice->events, PREVIEW_REQUEST);
    }
}

void app_voice_check_binding(app_voice_handle_t voice)
{
    if (voice) xEventGroupSetBits(voice->events, BIND_CHECK);
}

void app_voice_settings_open(app_voice_handle_t voice, bool open)
{
    if (!voice) return;
    if (open) {
        xEventGroupSetBits(voice->events, SETTINGS_OPEN);
        xEventGroupClearBits(voice->events, BUTTON_PRESSED);
    } else {
        xEventGroupClearBits(voice->events, SETTINGS_OPEN);
    }
}

static bool cloud_online(app_voice_handle_t voice, EventBits_t bits)
{
    uint32_t generation;
    return voice->chat && (bits & CLOUD_ONLINE) && app_wifi_link(voice->wifi, &generation) &&
           generation == voice->network_generation;
}

static bool recording(app_voice_handle_t voice)
{
    const EventBits_t bits = xEventGroupGetBits(voice->events);
    return (bits & BUTTON_HELD) && !(bits & BUTTON_RELEASED);
}

static bool turn_online(app_voice_handle_t voice)
{
    const EventBits_t bits = xEventGroupGetBits(voice->events);
    return cloud_online(voice, bits) && !(bits & TURN_FAILED);
}

static void button_down(void *button, void *arg)
{
    (void)button;
    app_voice_handle_t voice = arg;
    if (!(xEventGroupGetBits(voice->events) & SETTINGS_OPEN)) {
        xEventGroupSetBits(voice->events, BUTTON_HELD | BUTTON_PRESSED);
    }
}

static void button_up(void *button, void *arg)
{
    (void)button;
    app_voice_handle_t voice = arg;
    xEventGroupClearBits(voice->events, BUTTON_HELD);
    xEventGroupSetBits(voice->events, BUTTON_RELEASED);
}

static void chat_link(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    (void)data;
    app_voice_handle_t voice = arg;
    if (id == ESP_XIAOZHI_CHAT_EVENT_CONNECTED) {
        xEventGroupSetBits(voice->events, CLOUD_ONLINE);
        ESP_LOGI(TAG, "Xiaozhi cloud connected");
    } else if (id == ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(voice->events, CLOUD_ONLINE);
        xEventGroupSetBits(voice->events, TURN_FAILED);
    } else if (id == ESP_XIAOZHI_CHAT_EVENT_SERVER_GOODBYE) {
        xEventGroupSetBits(voice->events, REPLY_END);
    }
}

static void chat_event(esp_xiaozhi_chat_event_t event, void *data, void *arg)
{
    (void)data;
    app_voice_handle_t voice = arg;
    if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_SPEECH_STOPPED) {
        xEventGroupSetBits(voice->events, REPLY_END);
    } else if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_ERROR) {
        xEventGroupSetBits(voice->events, TURN_FAILED);
    }
}

static void receive_audio(const uint8_t *data, int length, void *arg)
{
    app_voice_handle_t voice = arg;
    if (!(xEventGroupGetBits(voice->events) & REPLY_ENABLED) || length <= 0) return;
    if (length > VOICE_MAX_PACKET_BYTES || xRingbufferSend(voice->reply, data, length, 0) != pdTRUE) {
        xEventGroupSetBits(voice->events, REPLY_OVERFLOW);
    }
}

static void close_chat(app_voice_handle_t voice)
{
    if (voice->chat) esp_xiaozhi_chat_deinit(voice->chat);
    if (voice->mcp) esp_mcp_destroy(voice->mcp);
    voice->chat = 0;
    voice->mcp = NULL;
    xEventGroupClearBits(voice->events, CLOUD_ONLINE);
}

static esp_err_t start_chat(app_voice_handle_t voice, const esp_xiaozhi_chat_info_t *info)
{
    if (!info->has_mqtt_config && !info->has_websocket_config) return ESP_ERR_INVALID_RESPONSE;

    // The transport uses an MCP engine internally. No device tools are registered.
    esp_mcp_t *mcp = NULL;
    esp_err_t err = esp_mcp_create(&mcp);
    if (err != ESP_OK) return err;
    esp_xiaozhi_chat_config_t config = ESP_XIAOZHI_CHAT_DEFAULT_CONFIG();
    config.has_mqtt_config = info->has_mqtt_config;
    config.has_websocket_config = info->has_websocket_config;
    config.mcp_engine = mcp;
    config.audio_callback = receive_audio;
    config.audio_callback_ctx = voice;
    config.event_callback = chat_event;
    config.event_callback_ctx = voice;
    err = esp_xiaozhi_chat_init(&config, &voice->chat);
    if (err != ESP_OK) {
        esp_mcp_destroy(mcp);
        return err;
    }
    voice->mcp = mcp;
    err = esp_xiaozhi_chat_start(voice->chat);
    if (err != ESP_OK) {
        close_chat(voice);
    } else {
        ESP_LOGI(TAG, "Cloud configuration ready (%s)", info->has_mqtt_config ? "MQTT" : "WebSocket");
    }
    return err;
}

static esp_err_t discover_cloud(app_voice_handle_t voice)
{
    if (voice->activation_pending && now_ms() < voice->activation_refresh) {
        esp_err_t err = voice_cloud_activate();
        if (err != ESP_OK) return err;
    }
    if (voice->snapshot.state == APP_VOICE_BINDING && now_ms() >= voice->activation_refresh) {
        publish(voice, APP_VOICE_BINDING, false, NULL);
    }
    voice->activation_pending = false;

    esp_xiaozhi_chat_info_t info = {0};
    esp_err_t err = esp_xiaozhi_chat_get_info(&info);
    if (err == ESP_OK && (info.has_activation_code || info.has_activation_challenge)) {
        const char *code = info.activation_code;
        if (!code || strlen(code) != APP_VOICE_BIND_CODE_BYTES - 1 || strspn(code, "0123456789") != strlen(code)) {
            code = NULL;
        }
        voice->activation_pending = info.has_activation_challenge;
        int refresh = info.activation_timeout_ms;
        if (refresh < ACTIVATE_POLL_MS || refresh > CODE_REFRESH_MS) refresh = CODE_REFRESH_MS;
        voice->activation_refresh = now_ms() + refresh;
        publish(voice, APP_VOICE_BINDING, false, code);
        err = ESP_ERR_NOT_FINISHED;
    } else if (err == ESP_OK) {
        err = start_chat(voice, &info);
    }
    esp_xiaozhi_chat_free_info(&info);
    return err;
}

static esp_err_t capture(app_voice_handle_t voice, bool flush, unsigned *frames)
{
    size_t size;
    esp_err_t err = voice_audio_capture(&voice->audio, flush, &size);
    if (err != ESP_OK || !size) return err;
    err = esp_xiaozhi_chat_send_audio_data(voice->chat, (const char *)voice->audio.encoded, size);
    if (err == ESP_OK) ++*frames;
    return err;
}

static esp_err_t play_reply(app_voice_handle_t voice, unsigned *frames)
{
    int64_t last_packet = now_ms();
    int64_t reply_end = 0;
    while (turn_online(voice)) {
        size_t size;
        uint8_t *packet = xRingbufferReceive(voice->reply, &size, pdMS_TO_TICKS(WORKER_POLL_MS));
        if (packet) {
            esp_err_t err = ESP_OK;
            if (!voice->audio.device) {
                err = voice_audio_open_speaker(&voice->audio, current_volume(voice));
                if (err == ESP_OK) publish(voice, APP_VOICE_SPEAKING, true, NULL);
            }
            if (err == ESP_OK) err = voice_audio_set_volume(&voice->audio, current_volume(voice));
            if (err == ESP_OK) err = voice_audio_play(&voice->audio, packet, size);
            vRingbufferReturnItem(voice->reply, packet);
            if (err != ESP_OK) return err;
            ++*frames;
            last_packet = now_ms();
            continue;
        }
        const EventBits_t bits = xEventGroupGetBits(voice->events);
        const int64_t now = now_ms();
        if (bits & REPLY_END) {
            if (!reply_end) reply_end = now;
            // The stop message and audio can arrive on different transports.
            // Wait for late packets and for the speaker's DMA to drain.
            if (now - reply_end >= REPLY_DRAIN_MS && now - last_packet >= REPLY_DRAIN_MS) {
                return bits & REPLY_OVERFLOW ? ESP_ERR_NO_MEM : ESP_OK;
            }
        } else if (now - last_packet >= REPLY_TIMEOUT_MS) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t run_turn(app_voice_handle_t voice, unsigned *sent, unsigned *played)
{
    const esp_xiaozhi_chat_audio_t format = {
        .format = "opus", .sample_rate = VOICE_INPUT_RATE, .channels = VOICE_CHANNELS,
        .frame_duration = VOICE_FRAME_MS,
    };
    xEventGroupClearBits(voice->events, BUTTON_RELEASED | TURN_FAILED | REPLY_END | REPLY_OVERFLOW);
    if (!recording(voice)) return ESP_OK;
    publish(voice, APP_VOICE_CONNECTING, true, NULL);
    esp_err_t err = esp_xiaozhi_chat_open_audio_channel(voice->chat, &format, NULL, 0);
    if (err != ESP_OK || !recording(voice)) return err;
    ESP_RETURN_ON_ERROR(voice_audio_open_mic(&voice->audio), TAG, "prepare capture");
    if (!recording(voice)) return ESP_OK;
    ESP_RETURN_ON_ERROR(esp_xiaozhi_chat_send_start_listening(voice->chat, ESP_XIAOZHI_CHAT_LISTENING_MODE_MANUAL),
                        TAG, "start listening");
    publish(voice, APP_VOICE_LISTENING, true, NULL);
    ESP_LOGI(TAG, "AI key capture started");
    const int64_t deadline = now_ms() + MAX_HOLD_MS;
    while (recording(voice) && turn_online(voice) && now_ms() < deadline) {
        ESP_RETURN_ON_ERROR(capture(voice, false, sent), TAG, "capture frame");
    }
    if (!turn_online(voice)) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(capture(voice, true, sent), TAG, "flush capture");
    voice_audio_close(&voice->audio);
    xEventGroupSetBits(voice->events, REPLY_ENABLED);
    ESP_RETURN_ON_ERROR(esp_xiaozhi_chat_send_stop_listening(voice->chat), TAG, "stop listening");
    if (!*sent) return ESP_OK;
    publish(voice, APP_VOICE_THINKING, true, NULL);
    ESP_LOGI(TAG, "AI key capture ended: %u Opus packets", *sent);
    return play_reply(voice, played);
}

static esp_err_t conversation(app_voice_handle_t voice)
{
    unsigned sent = 0, played = 0;
    esp_err_t err = run_turn(voice, &sent, &played);
    xEventGroupClearBits(voice->events, REPLY_ENABLED | BUTTON_PRESSED);
    voice_audio_close(&voice->audio);
    esp_xiaozhi_chat_close_audio_channel(voice->chat);
    size_t size;
    void *packet;
    while ((packet = xRingbufferReceive(voice->reply, &size, 0))) vRingbufferReturnItem(voice->reply, packet);

    if (err == ESP_OK && played) {
        portENTER_CRITICAL(&snapshot_lock);
        ++voice->snapshot.completed_turns;
        portEXIT_CRITICAL(&snapshot_lock);
    }
    ESP_LOGI(TAG, "Turn ended: sent=%u played=%u result=%s stack_free_min=%u bytes",
             sent, played, esp_err_to_name(err), (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return err;
}

static void voice_worker(void *arg)
{
    app_voice_handle_t voice = arg;
    esp_err_t setup = esp_event_handler_register(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID, chat_link, voice);
    if (setup != ESP_OK) ESP_LOGW(TAG, "Voice network unavailable: %s", esp_err_to_name(setup));
    int64_t cloud_retry = 0, notice_end = 0, next_bind_check = 0;
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(voice->events, BUTTON_PRESSED | PREVIEW_REQUEST | BIND_CHECK,
                                             pdTRUE, pdFALSE, pdMS_TO_TICKS(WORKER_POLL_MS));
        const int64_t now = now_ms();
        save_volume(voice);
        if ((bits & PREVIEW_REQUEST) && (bits & SETTINGS_OPEN)) {
            esp_err_t err = voice_audio_preview(&voice->audio, current_volume(voice));
            if (err != ESP_OK) ESP_LOGW(TAG, "Preview speaker volume: %s", esp_err_to_name(err));
        }
        uint32_t generation;
        const bool online = app_wifi_link(voice->wifi, &generation);
        if (generation != voice->network_generation) {
            close_chat(voice);
            voice->network_generation = generation;
            voice->activation_pending = false;
            cloud_retry = 0;
            notice_end = 0;
            publish(voice, APP_VOICE_IDLE, false, NULL);
        }
        if ((bits & BIND_CHECK) && now >= next_bind_check) {
            cloud_retry = 0;
            next_bind_check = now + ACTIVATE_POLL_MS;
        }
        if (setup == ESP_OK && online && !voice->chat && now >= cloud_retry) {
            esp_err_t err = discover_cloud(voice);
            cloud_retry = now_ms() + (err == ESP_ERR_NOT_FINISHED ? ACTIVATE_POLL_MS : CLOUD_RETRY_MS);
            if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
                ESP_LOGW(TAG, "Cloud discovery: %s", esp_err_to_name(err));
            }
        }

        const EventBits_t current = xEventGroupGetBits(voice->events);
        const bool ready = cloud_online(voice, current);
        const bool pressed = (bits & BUTTON_PRESSED) && !(current & SETTINGS_OPEN);
        if (pressed && ready) {
            if (conversation(voice) == ESP_OK) {
                publish(voice, APP_VOICE_IDLE, cloud_online(voice, xEventGroupGetBits(voice->events)), NULL);
            } else {
                publish(voice, APP_VOICE_ERROR, ready, NULL);
                notice_end = now_ms() + NOTICE_MS;
            }
        } else if (voice->snapshot.state == APP_VOICE_BINDING && online && !voice->chat) {
            continue;
        } else if (pressed) {
            app_voice_state_t state = setup == ESP_OK ? APP_VOICE_CONNECTING : APP_VOICE_ERROR;
            if (!online) state = APP_VOICE_NO_WIFI;
            publish(voice, state, false, NULL);
            notice_end = now_ms() + NOTICE_MS;
        } else if (now >= notice_end) {
            publish(voice, APP_VOICE_IDLE, ready, NULL);
        }
    }
}

esp_err_t app_voice_start(app_voice_handle_t *out, app_wifi_handle_t wifi)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (service.started) return ESP_ERR_INVALID_STATE;
    service.wifi = wifi;
    service.saved_volume = APP_VOICE_DEFAULT_VOLUME;
    nvs_handle_t settings;
    if (nvs_open(AUDIO_SETTINGS, NVS_READONLY, &settings) == ESP_OK) {
        uint8_t volume;
        if (nvs_get_u8(settings, "volume", &volume) == ESP_OK && volume <= APP_VOICE_MAX_VOLUME) {
            service.saved_volume = volume;
        }
        nvs_close(settings);
    }
    service.snapshot.volume = service.saved_volume;
    esp_err_t ret = ESP_ERR_NO_MEM;
    service.events = xEventGroupCreate();
    service.reply = xRingbufferCreateWithCaps(REPLY_BUFFER_BYTES, RINGBUF_TYPE_NOSPLIT,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!service.events || !service.reply) goto cleanup;
    button_handle_t buttons[BSP_BUTTON_NUM] = {0};
    ret = bsp_iot_button_create(buttons, NULL, BSP_BUTTON_NUM);
    service.button = buttons[BSP_BUTTON_AI];
    if (ret != ESP_OK) goto cleanup;
    ESP_GOTO_ON_ERROR(iot_button_register_cb(service.button, BUTTON_PRESS_DOWN, NULL, button_down, &service),
                      cleanup, TAG, "watch AI key press");
    ESP_GOTO_ON_ERROR(iot_button_register_cb(service.button, BUTTON_PRESS_UP, NULL, button_up, &service),
                      cleanup, TAG, "watch AI key release");
    // Volume and XiaoZhi transport settings write to NVS. SPI flash can
    // suspend the PSRAM cache, so this worker needs an internal stack.
    ESP_GOTO_ON_FALSE(xTaskCreateWithCaps(voice_worker, "pet_voice", WORKER_STACK_BYTES,
                                        &service, WORKER_PRIORITY, NULL,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) == pdPASS,
                      ESP_ERR_NO_MEM, cleanup, TAG, "start worker");
    service.started = true;
    *out = &service;
    return ESP_OK;

cleanup:
    if (service.button) iot_button_delete(service.button);
    if (service.reply) vRingbufferDeleteWithCaps(service.reply);
    if (service.events) vEventGroupDelete(service.events);
    memset(&service, 0, sizeof(service));
    return ret;
}
