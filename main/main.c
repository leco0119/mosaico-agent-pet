// SPDX-License-Identifier: Apache-2.0
#include "app_display.h"
#include "app_pet.h"
#include "app_voice.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "iris_screen_mirror.h"
#include "status_link.h"
#include "nvs_flash.h"
#include "ui_bundle.h"

static const char *TAG = "my_app";

/* The composition root retains owned handles, never controller internals. */
static struct {
    esp_gsp_handle_t ui;
    app_pet_handle_t controller;
    app_wifi_handle_t wifi;
    app_voice_handle_t voice;
} application;

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    /* Recovery must remain reachable even when the UI bundle cannot be loaded. */
    iris_ota_support_start();

    esp_gsp_config_t config;
    const esp_err_t err = ui_bundle_open(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GSP UI unavailable (0x%x); Recovery RPC remains active", err);
        return;
    }
    esp_gsp_esp_lcd_config_t display;
    const esp_err_t display_err = app_display_init(&display);
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "Display unavailable (0x%x); Recovery RPC remains active", display_err);
        return;
    }
    ESP_ERROR_CHECK(iris_screen_mirror_init());
    ESP_ERROR_CHECK(esp_gsp_esp_lcd_start(&config, &display, &application.ui));
    ESP_ERROR_CHECK(iris_screen_mirror_attach(application.ui));
    ESP_ERROR_CHECK(app_pet_create(application.ui, &application.controller));
    /* GSP retains the callback context. Device UI/controller live until reboot;
     * teardown requires quiescing GSP before app_pet_destroy(), then closing UI. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(app_wifi_start(&application.wifi));
    ESP_ERROR_CHECK_WITHOUT_ABORT(app_voice_start(&application.voice, application.wifi));
    ESP_ERROR_CHECK_WITHOUT_ABORT(status_link_start(application.controller, application.ui,
                                                  application.voice, application.wifi));
    ESP_LOGI(TAG, "Agent Pet ready");
}
