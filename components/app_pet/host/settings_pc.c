// SPDX-License-Identifier: Apache-2.0
#include "settings_pc.h"
#include <stdio.h>
#include <string.h>
#include "pet_settings.h"

enum { TICK_MS = 100, SCAN_TICKS = 3, JOIN_TICKS = 5 };

static struct {
    pet_settings_handle_t settings;
    void *timer;
    app_wifi_snapshot_t wifi;
    app_voice_snapshot_t voice;
    unsigned scan_in;
    unsigned join_in;
    unsigned bind_in;
    unsigned bind_checks;
    bool unbound;
    bool correct;
    char ssid[APP_WIFI_SSID_BYTES];
} mock;

static const app_wifi_ap_t nearby[] = {
    {.ssid = "Office", .rssi = -53, .secured = true, .saved = true},
    {.ssid = "Phone Hotspot", .rssi = -67, .secured = true},
    {.ssid = "访客网络", .rssi = -72},
    {.ssid = "Network 4", .rssi = -79, .secured = true},
    {.ssid = "Network 5", .rssi = -82, .secured = true},
    {.ssid = "Network 6", .rssi = -88},
};

static bool scan(void *ctx)
{
    (void)ctx;
    mock.wifi.scanning = true;
    mock.scan_in = SCAN_TICKS;
    ++mock.wifi.revision;
    fprintf(stderr, "settings_sim: scan\n");
    return true;
}

static bool connect(void *ctx, const char *ssid, const char *password)
{
    (void)ctx;
    if (mock.join_in) return false;
    mock.wifi.result = APP_WIFI_RESULT_PENDING;
    mock.wifi.connecting = true;
    mock.join_in = JOIN_TICKS;
    mock.correct = !password || !*password || strcmp(password, "test1234") == 0;
    snprintf(mock.ssid, sizeof(mock.ssid), "%s", ssid);
    ++mock.wifi.revision;
    fprintf(stderr, "settings_sim: connect %s\n", ssid);
    return true;
}

static void volume(void *ctx, unsigned percent, bool persist)
{
    (void)ctx;
    mock.voice.volume = percent;
    ++mock.voice.revision;
    fprintf(stderr, "settings_sim: volume=%u save=%d\n", percent, persist);
}

static void preview(void *ctx)
{
    (void)ctx;
    fprintf(stderr, "settings_sim: preview=%u\n", mock.voice.volume);
}

static void check_binding(void *ctx)
{
    (void)ctx;
    if (mock.voice.state == APP_VOICE_BINDING) {
        ++mock.bind_checks;
        if (mock.bind_checks == 1) {
            strcpy(mock.voice.binding_code, "519824");
        } else {
            mock.unbound = false;
            mock.voice.state = APP_VOICE_IDLE;
            mock.voice.ready = true;
            mock.voice.binding_code[0] = '\0';
        }
        ++mock.voice.revision;
    }
    fprintf(stderr, "settings_sim: binding check %u\n", mock.bind_checks);
}

static void navigation(void *ctx, bool open)
{
    (void)ctx;
    fprintf(stderr, "settings_sim: settings_open=%d\n", open);
}

static void complete_scan(void)
{
    mock.wifi.scanning = false;
    mock.wifi.count = 0;
    for (size_t i = 0; i < sizeof(nearby) / sizeof(nearby[0]); ++i) {
        if (strcmp(nearby[i].ssid, mock.wifi.ssid)) {
            mock.wifi.aps[mock.wifi.count++] = nearby[i];
        }
    }
    ++mock.wifi.revision;
}

static void complete_connect(void)
{
    mock.wifi.connecting = false;
    mock.wifi.result = mock.correct ? APP_WIFI_RESULT_CONNECTED : APP_WIFI_RESULT_AUTH;
    if (mock.correct) {
        mock.wifi.online = true;
        strcpy(mock.wifi.ssid, mock.ssid);
        if (mock.unbound) mock.bind_in = JOIN_TICKS;
    }
    ++mock.wifi.revision;
    fprintf(stderr, "settings_sim: join=%s\n", mock.correct ? "ok" : "auth");
}

static void tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    (void)ctx;
    if (mock.scan_in && --mock.scan_in == 0) complete_scan();
    if (mock.join_in && --mock.join_in == 0) complete_connect();
    if (mock.bind_in && --mock.bind_in == 0) {
        mock.voice.state = APP_VOICE_BINDING;
        strcpy(mock.voice.binding_code, "482617");
        ++mock.voice.revision;
        fprintf(stderr, "settings_sim: binding pending\n");
    }
    pet_settings_update(mock.settings, &mock.wifi, &mock.voice);
}

esp_gsp_err_t settings_pc_start(esp_gsp_handle_t ui, bool unbound)
{
    static const pet_settings_ops_t ops = {
        .scan = scan, .connect = connect, .volume = volume, .preview = preview,
        .check_binding = check_binding, .navigation = navigation,
    };
    memset(&mock, 0, sizeof(mock));
    mock.unbound = unbound;
    mock.wifi.online = !unbound;
    if (!unbound) strcpy(mock.wifi.ssid, "Home-2G");
    mock.voice.ready = !unbound;
    mock.voice.volume = APP_VOICE_DEFAULT_VOLUME;
    esp_gsp_err_t err = pet_settings_create(ui, &ops, NULL, &mock.settings);
    if (err != ESP_GSP_OK) return err;
    mock.timer = esp_gsp_timer_create(ui, TICK_MS, tick, NULL);
    return mock.timer ? ESP_GSP_OK : ESP_GSP_ERR_NO_MEM;
}

void settings_pc_stop(esp_gsp_handle_t ui)
{
    if (mock.timer) esp_gsp_timer_delete(ui, mock.timer);
    pet_settings_destroy(mock.settings);
}
