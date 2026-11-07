// SPDX-License-Identifier: Apache-2.0
#include "app_pet.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bundle_gsp.h"

#define PET_ANIMATIONS 4u
#define PET_HEARTBEAT_SECONDS 30u
#define PET_TICK_MS 1000u
#define PET_LINE_BYTES 128u
#define PET_VALUE_BYTES (PET_LINE_BYTES - 2u)

typedef struct {
    unsigned animation;
    char title[PET_VALUE_BYTES];
    char phrase[PET_VALUE_BYTES];
    char detail[PET_VALUE_BYTES];
} pet_host_view_t;

struct app_pet {
    esp_gsp_handle_t ui;
    void *timer;
    app_pet_snapshot_t snapshot;
    unsigned heartbeat_age;
    pet_host_view_t host;
    pet_host_view_t view;
    bool active;
    bool showing_voice;
    bool voice_ready;
};

static esp_gsp_err_t (*const show_pet[])(esp_gsp_handle_t, bool) = {
    gsp_pet_pet_0_set_visible, gsp_pet_pet_1_set_visible,
    gsp_pet_pet_2_set_visible, gsp_pet_pet_3_set_visible,
};

static void remember(app_pet_handle_t pet, esp_gsp_err_t err)
{
    if (err != ESP_GSP_OK) pet->snapshot.last_error = err;
}

static void select_animation(app_pet_handle_t pet, unsigned animation)
{
    /* Keep exactly one animation visible within the desktop page. */
    for (unsigned i = 0; i < PET_ANIMATIONS; ++i) {
        remember(pet, show_pet[i](pet->ui, i == animation));
    }
    pet->snapshot.animation = animation;
}

static void paint(app_pet_handle_t pet)
{
    if (!pet->active) return;
    pet->snapshot.last_error = ESP_GSP_OK;
    select_animation(pet, pet->view.animation);
    remember(pet, gsp_pet_st_title_set_text(pet->ui, pet->view.title));
    remember(pet, gsp_pet_st_phrase_set_text(pet->ui, pet->view.phrase));
    remember(pet, gsp_pet_st_detail_set_text(pet->ui, pet->view.detail));
}

static void render(app_pet_handle_t pet, const char *title, const char *phrase,
                   const char *detail, unsigned animation)
{
    snprintf(pet->view.title, sizeof(pet->view.title), "%s", title ? title : "");
    snprintf(pet->view.phrase, sizeof(pet->view.phrase), "%s", phrase ? phrase : "");
    snprintf(pet->view.detail, sizeof(pet->view.detail), "%s", detail ? detail : "");
    pet->view.animation = animation;
    pet->snapshot.animation = animation;
    paint(pet);
}

static void render_host(app_pet_handle_t pet)
{
    if (pet->showing_voice) return;
    if (!pet->snapshot.connected && pet->voice_ready) {
        render(pet, "Agent Pet", "空闲", "按住 AI 键说话", 0);
        return;
    }
    render(pet, pet->host.title, pet->host.phrase, pet->host.detail, pet->host.animation);
}

static void disconnected(app_pet_handle_t pet)
{
    pet->host.animation = 0;
    strcpy(pet->host.title, "Agent Pet");
    strcpy(pet->host.phrase, "等待连接");
    strcpy(pet->host.detail, "USB");
    pet->snapshot.connected = false;
    render_host(pet);
}

static void heartbeat_tick(esp_gsp_handle_t ui, void *context)
{
    (void)ui;
    app_pet_handle_t pet = context;
    if (pet->snapshot.connected && ++pet->heartbeat_age >= PET_HEARTBEAT_SECONDS) {
        disconnected(pet);
    }
}

esp_gsp_err_t app_pet_create(esp_gsp_handle_t ui, app_pet_handle_t *out)
{
    if (!out) return ESP_GSP_ERR_INVALID_ARG;
    *out = NULL;
    if (!ui) return ESP_GSP_ERR_INVALID_ARG;
    app_pet_handle_t pet = calloc(1, sizeof(*pet));
    if (!pet) return ESP_GSP_ERR_NO_MEM;
    pet->ui = ui;
    pet->active = true;
    select_animation(pet, 0);
    disconnected(pet);
    if (pet->snapshot.last_error != ESP_GSP_OK) {
        esp_gsp_err_t err = pet->snapshot.last_error;
        free(pet);
        return err;
    }
    pet->timer = esp_gsp_timer_create(ui, PET_TICK_MS, heartbeat_tick, pet);
    if (!pet->timer) {
        free(pet);
        return ESP_GSP_ERR_NO_MEM;
    }
    *out = pet;
    return ESP_GSP_OK;
}

esp_gsp_err_t app_pet_apply(app_pet_handle_t pet, const char *payload,
                          size_t size, unsigned *applied)
{
    if (!pet || !payload || !applied) return ESP_GSP_ERR_INVALID_ARG;
    *applied = 0;
    for (size_t pos = 0; pos < size;) {
        size_t end = pos;
        while (end < size && payload[end] != '\n' && payload[end] != '\r') ++end;
        size_t length = end - pos;
        if (length >= 2 && length < PET_LINE_BYTES && payload[pos + 1] == '=') {
            const char *value = payload + pos + 2;
            size_t value_size = length - 2;
            char *target = NULL;
            switch (payload[pos]) {
                case 'A':
                    if (value_size == 1 && value[0] >= '0' && value[0] <= '3') {
                        pet->host.animation = (unsigned)(value[0] - '0');
                        ++*applied;
                    }
                    break;
                case 'P': target = pet->host.phrase; break;
                case 'D': target = pet->host.detail; break;
                case 'T': target = pet->host.title; break;
                default: break;
            }
            if (target) {
                memcpy(target, value, value_size);
                target[value_size] = '\0';
                ++*applied;
            }
        }
        pos = end + 1;
    }
    if (*applied) {
        pet->heartbeat_age = 0;
        pet->snapshot.connected = true;
        ++pet->snapshot.updates;
        render_host(pet);
    }
    return pet->snapshot.last_error;
}

void app_pet_show_voice(app_pet_handle_t pet, const char *phrase,
                        const char *detail, unsigned animation, bool voice_ready)
{
    pet->showing_voice = phrase != NULL;
    pet->voice_ready = voice_ready;
    if (pet->showing_voice) {
        render(pet, "小智", phrase, detail, animation);
    } else {
        render_host(pet);
    }
}

esp_gsp_err_t app_pet_read(app_pet_handle_t pet, app_pet_snapshot_t *out)
{
    if (!pet || !out) return ESP_GSP_ERR_INVALID_ARG;
    *out = pet->snapshot;
    return ESP_GSP_OK;
}

void app_pet_set_active(app_pet_handle_t pet, bool active)
{
    if (!pet) return;
    pet->active = active;
    if (active) paint(pet);
}

esp_gsp_err_t app_pet_destroy(app_pet_handle_t *pet)
{
    if (!pet) return ESP_GSP_ERR_INVALID_ARG;
    if (!*pet) return ESP_GSP_OK;
    esp_gsp_err_t err = esp_gsp_timer_delete((*pet)->ui, (*pet)->timer);
    if (err != ESP_GSP_OK) return err;
    free(*pet);
    *pet = NULL;
    return ESP_GSP_OK;
}
