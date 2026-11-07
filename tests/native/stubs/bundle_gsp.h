#pragma once
#include "esp_gsp.h"
#include <stdio.h>

/* Stub only the external widget API. Tests execute the real UI controller. */
#define TEXT_CAPTURE(NAME, FIELD) \
    static inline esp_gsp_err_t gsp_usage_##NAME##_set_text(esp_gsp_handle_t ui, const char *text) \
    { snprintf(ui->FIELD, sizeof(ui->FIELD), "%s", text); return ESP_GSP_OK; }
#define TEXT_UNUSED(NAME) \
    static inline esp_gsp_err_t gsp_usage_##NAME##_set_text(esp_gsp_handle_t ui, const char *text) \
    { (void)ui; (void)text; return ESP_GSP_OK; }
#define VALUE_UNUSED(NAME) \
    static inline esp_gsp_err_t gsp_usage_##NAME##_set_value(esp_gsp_handle_t ui, int32_t value) \
    { (void)ui; (void)value; return ESP_GSP_OK; }
#define VISIBLE_UNUSED(NAME) \
    static inline esp_gsp_err_t gsp_usage_##NAME##_set_visible(esp_gsp_handle_t ui, bool visible) \
    { (void)ui; (void)visible; return ESP_GSP_OK; }
#define BAND(P, C) \
    VALUE_UNUSED(P##_ring_##C) VISIBLE_UNUSED(P##_ring_##C) \
    VALUE_UNUSED(P##_bar_##C) VISIBLE_UNUSED(P##_bar_##C)
#define VENDOR(P) \
    BAND(P, red) BAND(P, amber) BAND(P, green) \
    TEXT_CAPTURE(P##_ring_pct, P##_pct) TEXT_CAPTURE(P##_tokens, P##_tokens) \
    TEXT_CAPTURE(P##_ring_label, P##_window) TEXT_UNUSED(P##_bar_label) TEXT_UNUSED(P##_bar_pct) \
    TEXT_UNUSED(P##_bar_reset) TEXT_UNUSED(P##_reason)
VENDOR(r)
static inline esp_gsp_err_t gsp_usage_r_bar_group_set_visible(esp_gsp_handle_t ui, bool visible)
{ ui->r_bar_visible = visible; return ESP_GSP_OK; }
TEXT_CAPTURE(r_ring_reset, r_reset)
TEXT_CAPTURE(refresh_text, action)
VISIBLE_UNUSED(r_hint_red)
VISIBLE_UNUSED(r_hint_amber)
VISIBLE_UNUSED(r_hint_green)
TEXT_CAPTURE(status_text, status)
static inline bool gsp_usage_event_is_refresh(const esp_gsp_event_t *event)
{
    return event->refresh && event->scene_id == 1;
}
static inline bool gsp_pet_event_is_open_usage(const esp_gsp_event_t *event)
{ return event->type == ESP_GSP_EVENT_CALL && event->scene_id == 0 && event->action_id == 100; }
static inline bool gsp_usage_event_is_open_pet(const esp_gsp_event_t *event)
{ return event->type == ESP_GSP_EVENT_CALL && event->scene_id == 1 && event->action_id == 100; }
