// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "esp_err.h"
#include "usage_payload.h"

/* Private storage adapter. The caller owns the reusable buffer, initializes NVS
 * and serializes access. Uses only the application's usage_panel namespace. */
typedef struct {
    char buffer[USAGE_PAYLOAD_MAX];
} usage_store_t;

esp_err_t usage_store_load(usage_store_t *store, usage_snapshot_t *snapshot);
esp_err_t usage_store_save(usage_store_t *store, const usage_snapshot_t *snapshot);
