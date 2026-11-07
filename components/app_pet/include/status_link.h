// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "app_pet.h"
#include "app_voice.h"
#include "esp_err.h"
#include "app_usage.h"

/* Register screen-only pet RPCs once; pet/ui live until reboot.
 * UI updates drain on a GSP timer, never on the Iris worker task. */
esp_err_t status_link_start(app_pet_handle_t pet, esp_gsp_handle_t ui, app_voice_handle_t voice,
                            app_wifi_handle_t wifi);

/** Compose both UI pages. read_refresh is thread-safe and remains valid until reboot. */
esp_err_t status_link_start_with_usage(app_pet_handle_t pet, esp_gsp_handle_t ui,
    app_voice_handle_t voice, app_wifi_handle_t wifi, app_usage_handle_t usage,
    uint32_t (*read_refresh)(void *), void *refresh_ctx);
