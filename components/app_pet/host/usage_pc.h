// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "app_usage.h"

esp_gsp_err_t usage_pc_start(esp_gsp_handle_t ui, app_usage_handle_t controller);
void usage_pc_stop(esp_gsp_handle_t ui);
