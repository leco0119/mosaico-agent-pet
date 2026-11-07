// SPDX-License-Identifier: Apache-2.0
#include "usage_store.h"
#include "nvs.h"

static const char NAMESPACE[] = "usage_panel";
static const char SNAPSHOT_KEY[] = "snapshot_v1";

esp_err_t usage_store_load(usage_store_t *store, usage_snapshot_t *snapshot)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t length = sizeof(store->buffer);
    err = nvs_get_blob(handle, SNAPSHOT_KEY, store->buffer, &length);
    nvs_close(handle);
    if (err == ESP_OK && !usage_payload_parse(store->buffer, length, snapshot)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t usage_store_save(usage_store_t *store, const usage_snapshot_t *snapshot)
{
    /* An empty failed first observation must not replace an existing record. */
    if (!usage_snapshot_has_data(snapshot) && !snapshot->codex.ok && !snapshot->codex.quota_disabled) {
        return ESP_OK;
    }
    size_t length = usage_payload_encode(snapshot, store->buffer, sizeof(store->buffer));
    if (!length) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, SNAPSHOT_KEY, store->buffer, length);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
