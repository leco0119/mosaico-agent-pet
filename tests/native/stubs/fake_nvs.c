#include "nvs.h"
#include <assert.h>
#include <string.h>

/* Models only the external storage boundary: committed bytes survive a new
 * application context, while a failed commit retains the previous record. */
static unsigned char durable[2048], pending[2048];
static size_t durable_length, pending_length;
static int fail_commit;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out)
{
    assert(strcmp(name, "usage_panel") == 0);
    if (mode == NVS_READONLY && durable_length == 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = 1;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *length)
{
    assert(handle == 1 && strcmp(key, "snapshot_v1") == 0);
    if (durable_length == 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*length < durable_length) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(data, durable, durable_length);
    *length = durable_length;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length)
{
    assert(handle == 1 && strcmp(key, "snapshot_v1") == 0);
    assert(length <= sizeof(pending));
    memcpy(pending, data, length);
    pending_length = length;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    if (fail_commit) {
        return ESP_FAIL;
    }
    memcpy(durable, pending, pending_length);
    durable_length = pending_length;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1);
}

void fake_nvs_fail_commit(int fail)
{
    fail_commit = fail;
}

void fake_nvs_corrupt(void)
{
    durable[0] = 'x';
}
