// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "esp_xiaozhi_info.h"

/* Complete activation v1 using the identity owned by esp_xiaozhi. HTTP 202
 * maps to ESP_ERR_NOT_FINISHED while the user is entering the binding code. */
esp_err_t voice_cloud_activate(void);
