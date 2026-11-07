// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_pet.h"
#include "gsp_sim_bridge.h"
#include "settings_pc.h"
#include "pet_pages.h"
#include "usage_pc.h"

static struct {
    app_pet_handle_t pet;
    app_usage_handle_t usage;
    pet_pages_handle_t pages;
    void *timer;
    unsigned step;
} backend;

void gsp_bridge_app_deinit(esp_gsp_handle_t ui);

static void voice_demo_tick(esp_gsp_handle_t ui, void *context)
{
    (void)ui;
    (void)context;
    static const struct {
        const char *phrase;
        const char *detail;
        const char *host_status;
        unsigned animation;
        bool ready;
    } steps[] = {
        {"待绑定", "xiaozhi.me  123456", NULL, 2, false},
        {"正在听", "松开 AI 键发送", "A=1\nT=Codex\nP=运行命令\nD=exec_command", 2, true},
        {"思考中", "", NULL, 1, true},
        {"正在说话", "", "A=3\nT=WorkBuddy\nP=已完成\nD=--", 1, true},
        {NULL, NULL, NULL, 0, true},
    };
    if (backend.step >= sizeof(steps) / sizeof(steps[0])) return;
    unsigned step = backend.step++;
    app_pet_show_voice(backend.pet, steps[step].phrase, steps[step].detail, steps[step].animation, steps[step].ready);
    if (steps[step].host_status) {
        unsigned applied = 0;
        app_pet_apply(backend.pet, steps[step].host_status, strlen(steps[step].host_status), &applied);
    }
    app_pet_snapshot_t snapshot;
    if (app_pet_read(backend.pet, &snapshot) == ESP_GSP_OK) {
        fprintf(stderr, "pet_backend: voice_step=%u animation=%u updates=%lu last_error=%d\n", step,
                snapshot.animation, (unsigned long)snapshot.updates, (int)snapshot.last_error);
    }
}

static void demo_tick(esp_gsp_handle_t ui, void *context)
{
    (void)ui;
    (void)context;
    const char *mode = getenv("MOSAICO_PET_SIM_CYCLE");
    if (backend.step >= 4 && mode && strcmp(mode, "once") == 0) return;
    static const char *const payloads[] = {
        "A=0\nT=Agent Pet\nP=空闲\nD=USB",
        "A=1\nT=Codex\nP=运行命令\nD=exec_command",
        "A=2\nT=Codex\nP=等你确认\nD=request_user_input",
        "A=3\nT=Codex\nP=已完成\nD=--",
    };
    unsigned applied = 0;
    unsigned state = backend.step++ % 4;
    app_pet_apply(backend.pet, payloads[state], strlen(payloads[state]), &applied);
    fprintf(stderr, "pet_backend: animation=%u applied=%u\n", state, applied);
}

esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    esp_gsp_err_t err = app_pet_create(ui, &backend.pet);
    if (err != ESP_GSP_OK) return err;
    err = app_usage_create(ui, &backend.usage);
    fprintf(stderr, "pet_backend: usage controller init=%d\n", (int)err);
    if (err != ESP_GSP_OK) goto failed;
    const char *mode = getenv("MOSAICO_PET_SIM_CYCLE");
    bool unbound = mode && strcmp(mode, "settings-unbound") == 0;
    err = settings_pc_start(ui, unbound);
    if (err != ESP_GSP_OK) goto failed;
    err = pet_pages_create(ui, settings_pc_handle(), backend.pet, backend.usage, &backend.pages);
    if (err != ESP_GSP_OK) goto failed;
    err = usage_pc_start(ui, backend.usage);
    if (err != ESP_GSP_OK) goto failed;
    if (mode && !unbound && strcmp(mode, "settings") != 0) {
        void (*tick)(esp_gsp_handle_t, void *) = strcmp(mode, "voice") == 0 ? voice_demo_tick : demo_tick;
        tick(ui, NULL);
        backend.timer = esp_gsp_timer_create(ui, 2000, tick, NULL);
        if (!backend.timer) {
            err = ESP_GSP_ERR_NO_MEM;
            goto failed;
        }
    }
    return ESP_GSP_OK;
failed:
    gsp_bridge_app_deinit(ui);
    return err;
}

void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    pet_pages_destroy(&backend.pages);
    usage_pc_stop(ui);
    app_usage_destroy(&backend.usage);
    settings_pc_stop(ui);
    if (backend.timer) esp_gsp_timer_delete(ui, backend.timer);
    app_pet_snapshot_t snapshot;
    if (app_pet_read(backend.pet, &snapshot) == ESP_GSP_OK) {
        fprintf(stderr, "pet_backend: updates=%lu animation=%u connected=%d last_error=%d\n",
                (unsigned long)snapshot.updates, snapshot.animation,
                (int)snapshot.connected, (int)snapshot.last_error);
    }
    app_pet_destroy(&backend.pet);
}
