// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "esp_http_client.h"

/* esp_xiaozhi 0.1.3~1 supplies the device identity but omits the activation
 * version on its initial info request. Inject it only in that translation unit. */
static inline esp_http_client_handle_t pet_xiaozhi_http_init(const esp_http_client_config_t *config)
{
    esp_http_client_handle_t client = esp_http_client_init(config);
    if (client && esp_http_client_set_header(client, "Activation-Version", "1") != ESP_OK) {
        esp_http_client_cleanup(client);
        return NULL;
    }
    return client;
}

#define esp_http_client_init pet_xiaozhi_http_init
