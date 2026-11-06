// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "app_voice_state.h"
#include "app_wifi.h"
#include "esp_err.h"

typedef struct app_voice *app_voice_handle_t;

/* Start the application-lifetime service after NVS initialization. */
esp_err_t app_voice_start(app_voice_handle_t *out, app_wifi_handle_t wifi);
/* Copy the current state from any task. NULL handle returns an idle snapshot. */
void app_voice_read(app_voice_handle_t voice, app_voice_snapshot_t *out);
void app_voice_set_volume(app_voice_handle_t voice, unsigned volume, bool persist);
void app_voice_preview(app_voice_handle_t voice);
void app_voice_check_binding(app_voice_handle_t voice);
void app_voice_settings_open(app_voice_handle_t voice, bool open);
