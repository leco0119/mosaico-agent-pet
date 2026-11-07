// SPDX-License-Identifier: Apache-2.0
#include "app_usage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bundle_gsp.h"

#define TIMEOUT_MS 30000

/* 最长为更新时间 23 字节 + 两段 16 字节后缀 + 结尾 0；留出余量。 */
#define STATUS_TEXT_MAX 80

/* 环百分比与 tokens 缺失时的占位符（两处都在各自 font_charset 里）。 */
static const char PLACEHOLDER[] = "—";

struct app_usage {
    esp_gsp_handle_t ui;
    app_usage_refresh_cb_t refresh_cb;
    void *refresh_ctx;
    void *timeout_timer;
    app_usage_state_t state;
    usage_snapshot_t snapshot;
    esp_gsp_err_t last_error;
    bool active;
};

typedef esp_gsp_err_t (*value_setter_t)(esp_gsp_handle_t, int32_t);
typedef esp_gsp_err_t (*visible_setter_t)(esp_gsp_handle_t, bool);

/* 环和条各有红/橙/绿三个同位置对象
 * （GSP 的 arc/progress 指示色不能运行时修改），按 usage_band_t 下标只显示一个。 */
typedef struct {
    value_setter_t ring_value[USAGE_BAND_COUNT];
    visible_setter_t ring_visible[USAGE_BAND_COUNT];
    esp_gsp_err_t (*ring_pct)(esp_gsp_handle_t, const char *);
    esp_gsp_err_t (*ring_label)(esp_gsp_handle_t, const char *);
    esp_gsp_err_t (*bar_group_visible)(esp_gsp_handle_t, bool);   /* 整块 bar 容器 */
    value_setter_t bar_value[USAGE_BAND_COUNT];
    visible_setter_t bar_visible[USAGE_BAND_COUNT];
    esp_gsp_err_t (*bar_label)(esp_gsp_handle_t, const char *);
    esp_gsp_err_t (*bar_pct)(esp_gsp_handle_t, const char *);
    esp_gsp_err_t (*bar_reset)(esp_gsp_handle_t, const char *);
    esp_gsp_err_t (*tokens)(esp_gsp_handle_t, const char *);
    esp_gsp_err_t (*reason)(esp_gsp_handle_t, const char *);
} vendor_setters_t;

static const vendor_setters_t CODEX = {
    { gsp_usage_r_ring_red_set_value, gsp_usage_r_ring_amber_set_value, gsp_usage_r_ring_green_set_value },
    { gsp_usage_r_ring_red_set_visible, gsp_usage_r_ring_amber_set_visible, gsp_usage_r_ring_green_set_visible },
    gsp_usage_r_ring_pct_set_text, gsp_usage_r_ring_label_set_text,
    gsp_usage_r_bar_group_set_visible,
    { gsp_usage_r_bar_red_set_value, gsp_usage_r_bar_amber_set_value, gsp_usage_r_bar_green_set_value },
    { gsp_usage_r_bar_red_set_visible, gsp_usage_r_bar_amber_set_visible, gsp_usage_r_bar_green_set_visible },
    gsp_usage_r_bar_label_set_text, gsp_usage_r_bar_pct_set_text, gsp_usage_r_bar_reset_set_text,
    gsp_usage_r_tokens_set_text, gsp_usage_r_reason_set_text,
};

static void remember(struct app_usage *s, esp_gsp_err_t err)
{
    if (err != ESP_GSP_OK) {
        s->last_error = err;
    }
}

/* 三个同位置对象：先隐藏不用的，再给目标对象赋值并显示，避免中间帧两色叠加。
 * percent < 0 时显示值为 0 的绿色对象，让轨道保持可见。 */
static void apply_banded(struct app_usage *s, const value_setter_t *values,
                         const visible_setter_t *visibles, int percent)
{
    const usage_band_t band = usage_band_of(percent);
    for (int i = 0; i < USAGE_BAND_COUNT; ++i) {
        if ((usage_band_t)i != band) {
            remember(s, visibles[i](s->ui, false));
        }
    }
    remember(s, values[band](s->ui, percent < 0 ? 0 : percent));
    remember(s, visibles[band](s->ui, true));
}

static void apply_vendor(struct app_usage *s, const vendor_setters_t *set, const usage_vendor_t *v)
{
    char pct[16];   /* "%d%%" 最坏 13 字节，避开 format-truncation 警告 */
    if (v->ring_percent >= 0) {
        snprintf(pct, sizeof(pct), "%d%%", v->ring_percent);
    } else {
        snprintf(pct, sizeof(pct), "%s", PLACEHOLDER);
    }
    apply_banded(s, set->ring_value, set->ring_visible, v->ring_percent);
    remember(s, set->ring_pct(s->ui, pct));
    /* Keep v1 storage compatible while rendering window/reset on separate lines. */
    char title[USAGE_TEXT_MAX + 8];
    const char *separator = strstr(v->ring_label, " · ");
    const char *reset = "";
    if (separator) {
        snprintf(title, sizeof(title), "%.*s额度", (int)(separator - v->ring_label), v->ring_label);
        reset = separator + strlen(" · ");
    } else if (!v->ring_label[0] || strstr(v->ring_label, "重置")) {
        snprintf(title, sizeof(title), "额度");
        reset = v->ring_label;
    } else {
        snprintf(title, sizeof(title), "%s额度", v->ring_label);
    }
    remember(s, set->ring_label(s->ui, title));
    remember(s, gsp_usage_r_ring_reset_set_text(s->ui, reset));

    const bool bar = v->bar_percent >= 0;
    remember(s, set->bar_group_visible(s->ui, bar));
    if (bar) {
        snprintf(pct, sizeof(pct), "%d%%", v->bar_percent);
        apply_banded(s, set->bar_value, set->bar_visible, v->bar_percent);
        remember(s, set->bar_label(s->ui, v->bar_label));
        remember(s, set->bar_pct(s->ui, pct));
        remember(s, set->bar_reset(s->ui, v->bar_reset));
    }
    remember(s, set->tokens(s->ui, v->tokens_text[0] ? v->tokens_text : PLACEHOLDER));
    /* 原因行没有 visible 绑定：空文本即隐藏。 */
    const bool show_reason = !v->ok && v->reason[0];
    remember(s, set->reason(s->ui, show_reason ? v->reason : ""));
    const bool show_hint = v->ring_percent >= 0 && !bar && !show_reason;
    const usage_band_t band = usage_band_of(v->ring_percent);
    remember(s, gsp_usage_r_hint_red_set_visible(s->ui, show_hint && band == USAGE_BAND_RED));
    remember(s, gsp_usage_r_hint_amber_set_visible(s->ui, show_hint && band == USAGE_BAND_AMBER));
    remember(s, gsp_usage_r_hint_green_set_visible(s->ui, show_hint && band == USAGE_BAND_GREEN));
}

static void stop_timeout(struct app_usage *s)
{
    if (s->timeout_timer) {
        remember(s, esp_gsp_timer_delete(s->ui, s->timeout_timer));
        s->timeout_timer = NULL;
    }
}

static void timeout_tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    struct app_usage *s = ctx;
    stop_timeout(s);
    remember(s, app_usage_set_state(s, APP_USAGE_STATE_TIMEOUT));
}

void app_usage_handle_event(app_usage_handle_t s, const esp_gsp_event_t *event)
{
    if (!s || !event) return;
    if (!gsp_usage_event_is_refresh(event) || s->state == APP_USAGE_STATE_UPDATING) {
        return;   /* 更新中忽略重复点击 */
    }
    /* 定时器创建失败时 state 保持不变，下一次点击会再次尝试；PC 请求仍然发出。 */
    remember(s, app_usage_set_state(s, APP_USAGE_STATE_UPDATING));
    if (s->refresh_cb) {
        s->refresh_cb(s->refresh_ctx);
    }
}

esp_gsp_err_t app_usage_create(esp_gsp_handle_t ui, app_usage_handle_t *out_handle)
{
    if (!out_handle) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;
    if (!ui) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    struct app_usage *s = calloc(1, sizeof(*s));
    if (!s) {
        return ESP_GSP_ERR_NO_MEM;
    }
    s->ui = ui;
    s->snapshot = USAGE_SNAPSHOT_EMPTY;
    s->state = APP_USAGE_STATE_WAITING;
    *out_handle = s;
    return ESP_GSP_OK;
}

esp_gsp_err_t app_usage_set_active(app_usage_handle_t h, bool active)
{
    if (!h) return ESP_GSP_ERR_INVALID_ARG;
    h->active = active;
    if (!active) return ESP_GSP_OK;
    h->last_error = ESP_GSP_OK;
    apply_vendor(h, &CODEX, &h->snapshot.codex);
    remember(h, app_usage_set_state(h, h->state));
    return h->last_error;
}

esp_gsp_err_t app_usage_on_refresh(app_usage_handle_t h, app_usage_refresh_cb_t cb, void *ctx)
{
    if (!h) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    h->refresh_cb = cb;
    h->refresh_ctx = ctx;
    return ESP_GSP_OK;
}

esp_gsp_err_t app_usage_apply(app_usage_handle_t h, const usage_snapshot_t *snap)
{
    if (!h || !snap) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    h->last_error = ESP_GSP_OK;
    /* The transport resolves/coalesces observations. Merging again against an
     * older rendered frame could resurrect a successfully removed window. */
    h->snapshot = *snap;
    if (h->active) apply_vendor(h, &CODEX, &h->snapshot.codex);
    stop_timeout(h);
    remember(h, app_usage_set_state(h, h->snapshot.stale ? APP_USAGE_STATE_CACHED : APP_USAGE_STATE_UPDATED));
    return h->last_error;
}

esp_gsp_err_t app_usage_set_state(app_usage_handle_t h, app_usage_state_t state)
{
    if (!h) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    char text[STATUS_TEXT_MAX];
    char updated[USAGE_UPDATED_MAX];
    const char *stamp = h->snapshot.updated_text;
    const char *action = "点击更新";
    if (strncmp(stamp, "更新 ", strlen("更新 ")) == 0) {
        stamp += strlen("更新 ");
    }
    if (strlen(stamp) == 14 && strspn(stamp, "0123456789") == 8 && stamp[8] == ' ' && stamp[11] == ':') {
        snprintf(updated, sizeof(updated), "%.2s-%.2s%s", stamp + 4, stamp + 6, stamp + 8);
    } else {
        snprintf(updated, sizeof(updated), "%s", stamp);
    }
    switch (state) {
    case APP_USAGE_STATE_UPDATING:
        if (usage_snapshot_has_data(&h->snapshot)) {
            snprintf(text, sizeof(text), "%s%s历史数据", updated, updated[0] ? " · " : "");
        } else {
            snprintf(text, sizeof(text), "正在读取账户数据");
        }
        action = "更新中…";
        break;
    case APP_USAGE_STATE_UPDATED:
        snprintf(text, sizeof(text), "更新%s%s", updated[0] ? " " : "", updated);
        break;
    case APP_USAGE_STATE_CACHED:
        snprintf(text, sizeof(text), "%s%s历史数据", updated, updated[0] ? " · " : "");
        break;
    case APP_USAGE_STATE_TIMEOUT:
        snprintf(text, sizeof(text), "%s", usage_snapshot_has_data(&h->snapshot) ?
                 "PC 未响应 · 历史数据" : "PC 未响应");
        action = "点击重试";
        break;
    case APP_USAGE_STATE_WAITING:
    default:
        snprintf(text, sizeof(text), "%s", "等待 PC 连接");
        break;
    }
    /* 先写页脚，再武装超时：即使定时器失败用户也能看到"更新中…"。 */
    esp_gsp_err_t err = h->active ? gsp_usage_status_text_set_text(h->ui, text) : ESP_GSP_OK;
    const esp_gsp_err_t action_err = h->active ? gsp_usage_refresh_text_set_text(h->ui, action) : ESP_GSP_OK;
    if (err == ESP_GSP_OK) {
        err = action_err;
    }
    if (state != APP_USAGE_STATE_UPDATING) {
        /* 文本失败也必须离开 UPDATING（定时器此时已被删掉），否则点击永远被忽略。 */
        h->state = state;
        return err;
    }
    if (err != ESP_GSP_OK) {
        return err;
    }
    if (!h->timeout_timer) {
        h->timeout_timer = esp_gsp_timer_create(h->ui, TIMEOUT_MS, timeout_tick, h);
        if (!h->timeout_timer) {
            /* 无超时保护：state 不变，稍后的 app_usage_apply 会恢复页脚。 */
            return ESP_GSP_ERR_NO_MEM;
        }
    }
    h->state = state;
    return ESP_GSP_OK;
}

esp_gsp_err_t app_usage_destroy(app_usage_handle_t *handle)
{
    if (!handle) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    struct app_usage *s = *handle;
    if (!s) {
        return ESP_GSP_OK;
    }
    stop_timeout(s);
    free(s);
    *handle = NULL;
    return ESP_GSP_OK;
}
