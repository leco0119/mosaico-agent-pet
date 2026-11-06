// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_gsp.h"

typedef struct app_pet *app_pet_handle_t;
typedef struct {
    unsigned animation;        /* 0 idle, 1 working, 2 waiting, 3 done. */
    bool connected;            /* Host heartbeat received within 30 s. */
    uint32_t updates;
    esp_gsp_err_t last_error;
} app_pet_snapshot_t;

/* Create a controller borrowing ui; caller owns *out until destroy.
 * After creation, calls and timer dispatch belong to the UI task. */
esp_gsp_err_t app_pet_create(esp_gsp_handle_t ui, app_pet_handle_t *out);
/* Apply A/P/D/T fields on the UI task; payload is borrowed for this call.
 * Invalid fields are ignored; applied counts recognized valid fields. */
esp_gsp_err_t app_pet_apply(app_pet_handle_t pet, const char *payload,
                          size_t size, unsigned *applied);
/* Show voice over the pet on the UI task. A NULL phrase restores the current
 * host status. With no host, voice_ready selects the idle display. */
void app_pet_show_voice(app_pet_handle_t pet, const char *phrase,
                        const char *detail, unsigned animation, bool voice_ready);
/* Copy diagnostics on the UI task or after dispatch stops. */
esp_gsp_err_t app_pet_read(app_pet_handle_t pet, app_pet_snapshot_t *out);
/* Stop timers before freeing; ui outlives *pet. Retains it on failure. */
esp_gsp_err_t app_pet_destroy(app_pet_handle_t *pet);
