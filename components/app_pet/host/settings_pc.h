// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "esp_gsp.h"

esp_gsp_err_t settings_pc_start(esp_gsp_handle_t ui, bool unbound);
void settings_pc_stop(esp_gsp_handle_t ui);
