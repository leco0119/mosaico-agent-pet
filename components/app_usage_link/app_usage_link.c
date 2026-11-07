// SPDX-License-Identifier: Apache-2.0
#include "app_usage_link.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_iris.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "usage_store.h"

static const char *TAG = "usage_panel";

#define POLL_MS 250
#define LOCK_WAIT_MS 100

static const char OK_REPLY[] = "ok=1";
#define OK_REPLY_LEN (sizeof(OK_REPLY) - 1)

/* Private module context. `pending`/`pending_valid` are shared between the Iris
 * service task (handler) and the GSP render task (timer) under `lock`; the
 * snapshot buffers live here rather than on task stacks (larger than 128 bytes) so the
 * handler stays within the Iris task's stack budget. `staging` and
 * `refresh_seq` are touched by the render task only. */
static struct {
    app_usage_handle_t controller;
    SemaphoreHandle_t lock;
    usage_snapshot_t pending; /* latest parsed snapshot, not yet applied */
    bool pending_valid;
    usage_snapshot_t staging; /* render-task copy handed to app_usage_apply */
    usage_snapshot_t current; /* Iris-task owner; last merged observation */
    usage_store_t store;      /* reusable serialization buffer, Iris task only */
    atomic_uint refresh_seq;
} rpc;

/* Iris task context: parse straight into the slot, never touch the UI. */
static esp_err_t set_usage_handler(const esp_iris_rpc_request_t *request, uint8_t *response,
                                   size_t response_capacity, size_t *response_size, void *user_ctx)
{
    (void)user_ctx;
    if (response_capacity < OK_REPLY_LEN) {
        return ESP_ERR_NO_MEM; /* nothing is queued when the reply cannot be written */
    }
    if (xSemaphoreTake(rpc.lock, pdMS_TO_TICKS(LOCK_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    /* Parsing straight into the slot is safe: usage_payload_parse rejects on the
     * "v=1" header check before it writes *out, so a malformed payload leaves an
     * earlier, still unapplied snapshot intact. */
    const bool ok = usage_payload_parse((const char *)request->payload, request->payload_size,
                                        &rpc.pending);
    if (ok) {
        usage_snapshot_merge(&rpc.current, &rpc.pending);
        rpc.pending = rpc.current;
        rpc.pending_valid = true;
    }
    xSemaphoreGive(rpc.lock);
    if (!ok) {
        ESP_LOGW(TAG, "set_usage: invalid payload (%u bytes)", (unsigned)request->payload_size);
        return ESP_ERR_INVALID_ARG;
    }
    /* Iris serializes handlers. Persist outside the UI lock so flash writes
     * cannot block rendering; storage failure leaves the RAM snapshot usable. */
    const esp_err_t saved = usage_store_save(&rpc.store, &rpc.current);
    if (saved != ESP_OK) {
        ESP_LOGW(TAG, "snapshot save failed (0x%x)", saved);
    }
    memcpy(response, OK_REPLY, OK_REPLY_LEN);
    *response_size = OK_REPLY_LEN;
    ESP_LOGI(TAG, "set_usage: %u bytes accepted", (unsigned)request->payload_size);
    return ESP_OK;
}

/* GSP render task: move a pending snapshot to the UI. Never blocks; if the
 * handler holds the lock the snapshot is picked up on the next tick. */
static void poll_tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    (void)ctx;
    bool have_snapshot = false;
    if (xSemaphoreTake(rpc.lock, 0) == pdTRUE) {
        if (rpc.pending_valid) {
            rpc.staging = rpc.pending;
            rpc.pending_valid = false;
            have_snapshot = true;
        }
        xSemaphoreGive(rpc.lock);
    }
    if (have_snapshot) {
        const esp_gsp_err_t err = app_usage_apply(rpc.controller, &rpc.staging);
        if (err != ESP_GSP_OK) {
            ESP_LOGW(TAG, "apply failed (%d)", (int)err);
        }
    }
}

/* UI task: publish a counter; the existing pet heartbeat/status reply carries it. */
static void on_refresh(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "refresh_request seq=%u", atomic_fetch_add_explicit(&rpc.refresh_seq, 1, memory_order_relaxed) + 1);
}

esp_err_t app_usage_link_start(esp_gsp_handle_t ui, app_usage_handle_t controller)
{
    if (!ui || !controller) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rpc.lock) {
        return ESP_ERR_INVALID_STATE; /* already started; single instance per boot */
    }
    rpc.controller = controller;
    rpc.pending_valid = false;
    rpc.current = USAGE_SNAPSHOT_EMPTY;
    const esp_err_t restored = usage_store_load(&rpc.store, &rpc.current);
    if (restored == ESP_OK && usage_snapshot_has_data(&rpc.current)) {
        rpc.current.stale = true;
        rpc.pending = rpc.current;
        rpc.pending_valid = true;
        ESP_LOGI(TAG, "snapshot restored from NVS (historical data)");
    } else if (restored != ESP_OK && restored != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "snapshot unavailable (0x%x); waiting for PC", restored);
    }
    rpc.lock = xSemaphoreCreateMutex();
    if (!rpc.lock) {
        return ESP_ERR_NO_MEM;
    }
    if (app_usage_on_refresh(controller, on_refresh, NULL) != ESP_GSP_OK) {
        vSemaphoreDelete(rpc.lock);
        rpc.lock = NULL;
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_iris_rpc_register(APP_USAGE_LINK_SERVICE_ID, APP_USAGE_LINK_METHOD_SET_USAGE,
                                          set_usage_handler, NULL);
    if (err != ESP_OK) {
        app_usage_on_refresh(controller, NULL, NULL);
        vSemaphoreDelete(rpc.lock);
        rpc.lock = NULL;
        return err;
    }
    /* The timer lives until reboot, like the controller; no stop API is offered. */
    if (!esp_gsp_timer_create(ui, POLL_MS, poll_tick, NULL)) {
        esp_iris_rpc_unregister(APP_USAGE_LINK_SERVICE_ID, APP_USAGE_LINK_METHOD_SET_USAGE);
        app_usage_on_refresh(controller, NULL, NULL);
        /* Theoretical window: a set_usage handler already dispatched before the
         * unregister could still hold `lock` here. This path only runs during
         * startup, before the PC has anything to push, and the caller aborts on
         * the error via ESP_ERROR_CHECK, so it is accepted rather than fenced. */
        vSemaphoreDelete(rpc.lock);
        rpc.lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "RPC 0x%04x/%u ready", APP_USAGE_LINK_SERVICE_ID, APP_USAGE_LINK_METHOD_SET_USAGE);
    return ESP_OK;
}

uint32_t app_usage_link_refresh_sequence(void *ctx)
{
    (void)ctx;
    return atomic_load_explicit(&rpc.refresh_seq, memory_order_relaxed);
}
