// SPDX-License-Identifier: Apache-2.0
#include "pet_pages.h"
#include <stdlib.h>
#ifndef ESP_PLATFORM
#include <stdio.h>
#endif
#include "bundle_gsp.h"

enum { PET_SCENE = 0, USAGE_SCENE = 1 };

struct pet_pages {
    esp_gsp_handle_t ui;
    pet_settings_handle_t settings;
    app_usage_handle_t usage;
    app_pet_handle_t pet;
};

static void on_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event, void *ctx)
{
    pet_pages_handle_t pages = ctx;
    if (event->type == ESP_GSP_EVENT_SCENE_CHANGED) {
        app_pet_set_active(pages->pet, event->scene_id == PET_SCENE);
        app_usage_set_active(pages->usage, event->scene_id == USAGE_SCENE);
        pet_settings_set_active(pages->settings, event->scene_id == PET_SCENE);
    } else if (gsp_pet_event_is_open_usage(event)) {
        esp_gsp_goto_scene(ui, USAGE_SCENE, ESP_GSP_SLIDE_LEFT);
    } else if (gsp_usage_event_is_open_pet(event)) {
        esp_gsp_goto_scene(ui, PET_SCENE, ESP_GSP_SLIDE_RIGHT);
    } else if (event->scene_id == PET_SCENE) {
        pet_settings_handle_event(pages->settings, event);
    } else if (event->scene_id == USAGE_SCENE) {
        app_usage_handle_event(pages->usage, event);
    }
#ifndef ESP_PLATFORM
    if (getenv("USAGE_PANEL_CAPTURE")) {
        esp_gsp_err_t err = esp_gsp_flush(ui, 1000);
        fprintf(stderr, "pet_pages: event type=%u scene=%u action=%u error=%d\n",
                (unsigned)event->type, event->scene_id, event->action_id, (int)err);
        fflush(stderr);
    }
#endif
}

esp_gsp_err_t pet_pages_create(esp_gsp_handle_t ui, pet_settings_handle_t settings,
    app_pet_handle_t pet, app_usage_handle_t usage, pet_pages_handle_t *out)
{
    if (!out) return ESP_GSP_ERR_INVALID_ARG;
    *out = NULL;
    if (!ui || !settings || !pet || !usage) return ESP_GSP_ERR_INVALID_ARG;
    pet_pages_handle_t pages = calloc(1, sizeof(*pages));
    if (!pages) return ESP_GSP_ERR_NO_MEM;
    pages->ui = ui;
    pages->settings = settings;
    pages->usage = usage;
    pages->pet = pet;
    esp_gsp_err_t err = esp_gsp_set_swipe_enabled(ui, true);
    if (err == ESP_GSP_OK) err = esp_gsp_on_event(ui, on_event, pages);
    if (err != ESP_GSP_OK) {
        free(pages);
        return err;
    }
    *out = pages;
    return ESP_GSP_OK;
}

esp_gsp_err_t pet_pages_destroy(pet_pages_handle_t *handle)
{
    if (!handle) return ESP_GSP_ERR_INVALID_ARG;
    if (!*handle) return ESP_GSP_OK;
    esp_gsp_err_t err = esp_gsp_on_event((*handle)->ui, NULL, NULL);
    if (err != ESP_GSP_OK) return err;
    free(*handle);
    *handle = NULL;
    return ESP_GSP_OK;
}
