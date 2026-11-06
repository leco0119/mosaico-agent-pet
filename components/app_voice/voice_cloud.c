// SPDX-License-Identifier: Apache-2.0
#include "voice_cloud.h"

#include <stdio.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "nvs.h"
#include "sdkconfig.h"

esp_err_t voice_cloud_activate(void)
{
    // Match the identity used by esp_xiaozhi_chat_get_info in version 0.1.3~1.
    char client_id[37];
    size_t length = sizeof(client_id);
    nvs_handle_t identity;
    esp_err_t err = nvs_open("board", NVS_READONLY, &identity);
    if (err != ESP_OK) return err;
    err = nvs_get_str(identity, "uuid", client_id, &length);
    nvs_close(identity);
    if (err != ESP_OK) return err;

    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_BASE);
    if (err != ESP_OK) return err;
    char device_id[18];
    snprintf(device_id, sizeof(device_id), MACSTR, MAC2STR(mac));
    const esp_http_client_config_t config = {
        .url = CONFIG_XIAOZHI_OTA_URL "activate",
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_XIAOZHI_INFO_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) return ESP_ERR_NO_MEM;
    err = esp_http_client_set_header(http, "Device-Id", device_id);
    if (err == ESP_OK) err = esp_http_client_set_header(http, "Client-Id", client_id);
    if (err == ESP_OK) err = esp_http_client_set_header(http, "Activation-Version", "1");
    if (err == ESP_OK) err = esp_http_client_set_header(http, "User-Agent", CONFIG_IDF_TARGET);
    if (err == ESP_OK) err = esp_http_client_set_header(http, "Content-Type", "application/json");
    if (err == ESP_OK) err = esp_http_client_set_post_field(http, "{}", 2);
    if (err == ESP_OK) err = esp_http_client_perform(http);
    if (err == ESP_OK) {
        const int status = esp_http_client_get_status_code(http);
        if (status == 202) err = ESP_ERR_NOT_FINISHED;
        else if (status != 200) err = ESP_ERR_INVALID_RESPONSE;
    }
    esp_http_client_cleanup(http);
    return err;
}
