// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_codec_dev.h"
#include "esp_err.h"

enum {
    VOICE_INPUT_RATE = 16000,
    VOICE_OUTPUT_RATE = 24000,
    VOICE_FRAME_MS = 60,
    VOICE_CHANNELS = 1,
    VOICE_MAX_PACKET_BYTES = 1400,
};

typedef struct {
    esp_codec_dev_handle_t device;
    void *encoder;
    void *decoder;
    uint8_t *pcm;
    uint8_t *encoded;
    int input_bytes;
    int output_bytes;
    int volume;
    unsigned recorded;
} voice_audio_t;

esp_err_t voice_audio_open_mic(voice_audio_t *audio);
/* Reads one short PCM slice or flushes a padded final frame. The encoded
 * packet, if produced, is valid until the next call. */
esp_err_t voice_audio_capture(voice_audio_t *audio, bool flush, size_t *size);
esp_err_t voice_audio_open_speaker(voice_audio_t *audio, unsigned volume);
esp_err_t voice_audio_set_volume(voice_audio_t *audio, unsigned volume);
esp_err_t voice_audio_preview(voice_audio_t *audio, unsigned volume);
esp_err_t voice_audio_play(voice_audio_t *audio, uint8_t *packet, size_t size);
void voice_audio_close(voice_audio_t *audio);
