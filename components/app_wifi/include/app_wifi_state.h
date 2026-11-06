// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    APP_WIFI_SSID_BYTES = 33,
    APP_WIFI_PASSWORD_BYTES = 65,
    APP_WIFI_MAX_APS = 16,
};

typedef enum {
    APP_WIFI_RESULT_NONE,
    APP_WIFI_RESULT_PENDING,
    APP_WIFI_RESULT_CONNECTED,
    APP_WIFI_RESULT_AUTH,
    APP_WIFI_RESULT_UNREACHABLE,
    APP_WIFI_RESULT_SAVE_FAILED,
} app_wifi_result_t;

typedef struct {
    char ssid[APP_WIFI_SSID_BYTES];
    int8_t rssi;                       /* dBm */
    bool secured;
    bool saved;
} app_wifi_ap_t;

typedef struct {
    uint32_t revision;
    bool online;
    bool connecting;
    bool scanning;
    bool scan_failed;
    app_wifi_result_t result;          /* Most recent user-initiated connection. */
    char ssid[APP_WIFI_SSID_BYTES];     /* Active or pending station network. */
    uint8_t count;
    app_wifi_ap_t aps[APP_WIFI_MAX_APS];
} app_wifi_snapshot_t;
