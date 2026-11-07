// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "app_usage.h"
#include "app_pet.h"
#include "pet_settings.h"

typedef struct pet_pages *pet_pages_handle_t;

/** Sole event owner. Controllers outlive this router. Call after their creation. */
esp_gsp_err_t pet_pages_create(esp_gsp_handle_t ui, pet_settings_handle_t settings,
    app_pet_handle_t pet, app_usage_handle_t usage, pet_pages_handle_t *out);
/** Quiesce GSP before destroying the router, then destroy its controllers. */
esp_gsp_err_t pet_pages_destroy(pet_pages_handle_t *pages);
