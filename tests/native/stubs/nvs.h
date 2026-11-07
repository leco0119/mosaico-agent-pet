#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
void fake_nvs_fail_commit(int fail);
void fake_nvs_corrupt(void);
