#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_gsp_err_t;
#define ESP_GSP_OK 0
#define ESP_GSP_ERR_INVALID_ARG -1
#define ESP_GSP_ERR_NO_MEM -2
typedef struct fake_ui *esp_gsp_handle_t;
enum { ESP_GSP_EVENT_CALL, ESP_GSP_EVENT_SCENE_CHANGED };
enum { ESP_GSP_SLIDE_LEFT, ESP_GSP_SLIDE_RIGHT };
typedef struct { bool refresh; unsigned type, scene_id, action_id; } esp_gsp_event_t;
typedef void (*fake_event_cb_t)(esp_gsp_handle_t, const esp_gsp_event_t *, void *);
typedef void (*fake_timer_cb_t)(esp_gsp_handle_t, void *);
struct fake_ui {
    char r_pct[16], r_tokens[16], status[80];
    char r_window[64], r_reset[64], action[32];
    bool r_bar_visible;
    bool swipe_enabled;
    unsigned scene;
    fake_event_cb_t event_cb;
    void *event_ctx;
    fake_timer_cb_t timer_cb;
    void *timer_ctx;
};
static inline esp_gsp_err_t esp_gsp_set_swipe_enabled(esp_gsp_handle_t ui, bool enabled)
{ ui->swipe_enabled = enabled; return ESP_GSP_OK; }
static inline esp_gsp_err_t esp_gsp_flush(esp_gsp_handle_t ui, unsigned timeout)
{ (void)ui; (void)timeout; return ESP_GSP_OK; }
static inline esp_gsp_err_t esp_gsp_goto_scene(esp_gsp_handle_t ui, unsigned scene, unsigned transition)
{
    (void)transition;
    ui->scene = scene;
    esp_gsp_event_t event = {.type = ESP_GSP_EVENT_SCENE_CHANGED, .scene_id = scene};
    if (ui->event_cb) ui->event_cb(ui, &event, ui->event_ctx);
    return ESP_GSP_OK;
}
static inline esp_gsp_err_t esp_gsp_on_event(esp_gsp_handle_t ui, fake_event_cb_t cb, void *ctx)
{
    ui->event_cb = cb;
    ui->event_ctx = ctx;
    return ESP_GSP_OK;
}
static inline void *esp_gsp_timer_create(esp_gsp_handle_t ui, int ms, fake_timer_cb_t cb, void *ctx)
{
    (void)ms;
    ui->timer_cb = cb;
    ui->timer_ctx = ctx;
    return ui;
}
static inline esp_gsp_err_t esp_gsp_timer_delete(esp_gsp_handle_t ui, void *timer)
{
    (void)timer;
    ui->timer_cb = NULL;
    ui->timer_ctx = NULL;
    return ESP_GSP_OK;
}
