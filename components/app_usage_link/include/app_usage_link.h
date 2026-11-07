// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "app_usage.h"
#include "esp_err.h"
#include "esp_gsp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_USAGE_LINK_SERVICE_ID 0x5550u /* 'U''P' */
#define APP_USAGE_LINK_METHOD_SET_USAGE 1u

/** Register the set_usage RPC and a 250 ms GSP timer that hands received
 * snapshots to the controller on the UI thread. Call once from app_main
 * after app_usage_create. The controller must outlive this component. */
esp_err_t app_usage_link_start(esp_gsp_handle_t ui, app_usage_handle_t controller);

/** Thread-safe refresh counter for the shared pet transport; ctx is unused. */
uint32_t app_usage_link_refresh_sequence(void *ctx);

#ifdef __cplusplus
}
#endif
