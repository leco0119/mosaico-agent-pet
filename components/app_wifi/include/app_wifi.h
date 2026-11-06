// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "app_wifi_state.h"
#include "esp_err.h"

typedef struct app_wifi *app_wifi_handle_t;

/* Start the application's station and its single radio/credential worker. */
esp_err_t app_wifi_start(app_wifi_handle_t *out);
void app_wifi_read(app_wifi_handle_t wifi, app_wifi_snapshot_t *out);
/* Return link availability and a generation that changes across reconnects. */
bool app_wifi_link(app_wifi_handle_t wifi, uint32_t *generation);
esp_err_t app_wifi_scan(app_wifi_handle_t wifi);
/* A NULL password selects a saved credential. An empty string selects an open network. */
esp_err_t app_wifi_connect(app_wifi_handle_t wifi, const char *ssid, const char *password);
