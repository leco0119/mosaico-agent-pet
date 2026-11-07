// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include "esp_gsp.h"
#include "usage_payload.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque UI controller; ownership and threading contracts: ../README.md. */
typedef struct app_usage *app_usage_handle_t;

typedef enum {
    APP_USAGE_STATE_WAITING = 0,   /* 等待 PC… · 点此更新 */
    APP_USAGE_STATE_UPDATING,      /* 更新中… */
    APP_USAGE_STATE_UPDATED,       /* 更新 HH:MM · 点此更新 */
    APP_USAGE_STATE_TIMEOUT,       /* PC 未响应 · 点此重试 */
    APP_USAGE_STATE_CACHED,        /* 更新 HH:MM · 历史数据 · 点此更新 */
} app_usage_state_t;

/** 点击"更新"时在 UI 线程回调；宿主在这里打日志标记或喂假数据。 */
typedef void (*app_usage_refresh_cb_t)(void *ctx);

/** Create the controller for ui; the caller owns *out_handle.
 * ui must outlive the controller. Clears *out_handle on failure. */
esp_gsp_err_t app_usage_create(esp_gsp_handle_t ui, app_usage_handle_t *out_handle);

/** Forward scene-qualified events from the application's sole event dispatcher. */
void app_usage_handle_event(app_usage_handle_t h, const esp_gsp_event_t *event);
/** Starts inactive. Cache updates off-screen and repaint when its scene is entered. */
esp_gsp_err_t app_usage_set_active(app_usage_handle_t h, bool active);

/** Register the refresh hook; cb may be NULL to clear. UI thread only. */
esp_gsp_err_t app_usage_on_refresh(app_usage_handle_t h, app_usage_refresh_cb_t cb, void *ctx);

/** Write an already-merged snapshot, cancel timeout and set UPDATED/CACHED.
 * The host/transport owns usage_snapshot_merge; this is an authoritative handoff.
 * UI thread only (call from a GSP timer or event callback). */
esp_gsp_err_t app_usage_apply(app_usage_handle_t h, const usage_snapshot_t *snap);

/** Switch the footer state text. UPDATING starts the 30 s timeout. UI thread only. */
esp_gsp_err_t app_usage_set_state(app_usage_handle_t h, app_usage_state_t state);

/** Caller first detaches the event dispatcher; release timers and clear *handle. */
esp_gsp_err_t app_usage_destroy(app_usage_handle_t *handle);

#ifdef __cplusplus
}
#endif
