// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    APP_VOICE_BIND_CODE_BYTES = 7,
    APP_VOICE_DEFAULT_VOLUME = 50,
    APP_VOICE_MAX_VOLUME = 100,
};

typedef enum {
    APP_VOICE_IDLE,
    APP_VOICE_NO_WIFI,
    APP_VOICE_CONNECTING,
    APP_VOICE_BINDING,
    APP_VOICE_LISTENING,
    APP_VOICE_THINKING,
    APP_VOICE_SPEAKING,
    APP_VOICE_ERROR,
} app_voice_state_t;

typedef struct {
    app_voice_state_t state;
    bool ready;
    uint8_t volume;
    uint32_t revision;
    uint32_t completed_turns;
    char binding_code[APP_VOICE_BIND_CODE_BYTES];
} app_voice_snapshot_t;
