// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "app_voice_state.h"
#include "app_wifi_state.h"
#include "esp_gsp.h"

typedef struct pet_settings *pet_settings_handle_t;

typedef struct {
    bool (*scan)(void *ctx);
    bool (*connect)(void *ctx, const char *ssid, const char *password);
    void (*volume)(void *ctx, unsigned percent, bool persist);
    void (*preview)(void *ctx);
    void (*check_binding)(void *ctx);
    void (*navigation)(void *ctx, bool settings_open);
} pet_settings_ops_t;

/* One instance per pet runtime. Dispatch and updates run on the GSP task. */
esp_gsp_err_t pet_settings_create(esp_gsp_handle_t ui, const pet_settings_ops_t *ops, void *ctx,
                                  pet_settings_handle_t *out);
void pet_settings_update(pet_settings_handle_t settings, const app_wifi_snapshot_t *wifi,
                         const app_voice_snapshot_t *voice);
/* Caller quiesces GSP before freeing the controller and list callback context. */
void pet_settings_destroy(pet_settings_handle_t settings);
