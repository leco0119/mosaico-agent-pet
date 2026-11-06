// SPDX-License-Identifier: Apache-2.0
#include "voice_audio.h"

#include <stdlib.h>
#include <string.h>
#include "bsp/audio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    PCM_BITS = 16,
    CAPTURE_SLICE_MS = 20,
    CAPTURE_SLICE_BYTES = VOICE_INPUT_RATE * CAPTURE_SLICE_MS * sizeof(int16_t) / 1000,
    CAPTURE_FRAME_BYTES = VOICE_INPUT_RATE * VOICE_FRAME_MS * sizeof(int16_t) / 1000,
    OPUS_MAX_FRAME_MS = 120,
    PCM_BYTES = VOICE_OUTPUT_RATE * OPUS_MAX_FRAME_MS * sizeof(int16_t) / 1000,
    MIC_GAIN_DB = 30,
    PREVIEW_WARMUP_MS = 180,
    PREVIEW_BLOCK_SAMPLES = VOICE_OUTPUT_RATE * VOICE_FRAME_MS / 1000,
    PREVIEW_BLOCKS = 6,
    PREVIEW_DRAIN_MS = 120,
    PREVIEW_PERIOD = 40,  // 600 Hz at VOICE_OUTPUT_RATE.
    PREVIEW_RAMP = 240,
    PREVIEW_AMPLITUDE = 10000,
};

static const char *TAG = "pet_voice_audio";

static esp_err_t open_device(voice_audio_t *audio, esp_codec_dev_handle_t device, unsigned rate)
{
    if (!device) return ESP_FAIL;
    // The I2S driver copies PCM through its own DMA buffers.
    if (!audio->pcm) audio->pcm = heap_caps_malloc(PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio->pcm) return ESP_ERR_NO_MEM;
    esp_codec_dev_sample_info_t format = {
        .sample_rate = rate, .channel = VOICE_CHANNELS, .bits_per_sample = PCM_BITS,
    };
    if (esp_codec_dev_open(device, &format) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    audio->device = device;
    return ESP_OK;
}

esp_err_t voice_audio_open_mic(voice_audio_t *audio)
{
    esp_opus_enc_config_t config = {
        .sample_rate = VOICE_INPUT_RATE, .channel = VOICE_CHANNELS, .bits_per_sample = PCM_BITS,
        .bitrate = ESP_OPUS_BITRATE_AUTO, .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP, .complexity = 2, .enable_vbr = true,
    };
    esp_err_t ret = ESP_FAIL;
    ESP_GOTO_ON_FALSE(esp_opus_enc_open(&config, sizeof(config), &audio->encoder) == ESP_AUDIO_ERR_OK,
                      ESP_FAIL, fail, TAG, "open Opus encoder");
    ESP_GOTO_ON_FALSE(esp_opus_enc_get_frame_size(audio->encoder, &audio->input_bytes, &audio->output_bytes)
                      == ESP_AUDIO_ERR_OK && audio->input_bytes == CAPTURE_FRAME_BYTES && audio->output_bytes > 0,
                      ESP_ERR_INVALID_SIZE, fail, TAG, "invalid Opus frame size");
    audio->encoded = malloc(audio->output_bytes);
    ESP_GOTO_ON_FALSE(audio->encoded, ESP_ERR_NO_MEM, fail, TAG, "allocate Opus frame");
    ESP_GOTO_ON_ERROR(open_device(audio, bsp_audio_codec_microphone_init(), VOICE_INPUT_RATE),
                      fail, TAG, "open microphone");
    ESP_GOTO_ON_FALSE(esp_codec_dev_set_in_gain(audio->device, MIC_GAIN_DB) == ESP_CODEC_DEV_OK,
                      ESP_FAIL, fail, TAG, "set microphone gain");
    return ESP_OK;

fail:
    voice_audio_close(audio);
    return ret;
}

esp_err_t voice_audio_capture(voice_audio_t *audio, bool flush, size_t *size)
{
    *size = 0;
    if (flush) {
        if (!audio->recorded) return ESP_OK;
        memset(audio->pcm + audio->recorded, 0, audio->input_bytes - audio->recorded);
    } else {
        if (esp_codec_dev_read(audio->device, audio->pcm + audio->recorded, CAPTURE_SLICE_BYTES)
                != ESP_CODEC_DEV_OK) return ESP_FAIL;
        audio->recorded += CAPTURE_SLICE_BYTES;
        if (audio->recorded < (unsigned)audio->input_bytes) return ESP_OK;
    }
    esp_audio_enc_in_frame_t input = {.buffer = audio->pcm, .len = audio->input_bytes};
    esp_audio_enc_out_frame_t output = {.buffer = audio->encoded, .len = audio->output_bytes};
    audio->recorded = 0;
    if (esp_opus_enc_process(audio->encoder, &input, &output) != ESP_AUDIO_ERR_OK) return ESP_FAIL;
    if (output.encoded_bytes > VOICE_MAX_PACKET_BYTES) return ESP_ERR_INVALID_SIZE;
    *size = output.encoded_bytes;
    return ESP_OK;
}

static esp_err_t open_speaker(voice_audio_t *audio, unsigned volume)
{
    esp_err_t err = open_device(audio, bsp_audio_codec_speaker_init(), VOICE_OUTPUT_RATE);
    audio->volume = -1;
    return err == ESP_OK ? voice_audio_set_volume(audio, volume) : err;
}

esp_err_t voice_audio_set_volume(voice_audio_t *audio, unsigned volume)
{
    if (audio->volume == (int)volume) return ESP_OK;
    if (esp_codec_dev_set_out_vol(audio->device, (int)volume) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_mute(audio->device, volume == 0) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    audio->volume = (int)volume;
    return ESP_OK;
}

esp_err_t voice_audio_open_speaker(voice_audio_t *audio, unsigned volume)
{
    esp_opus_dec_cfg_t config = {
        .sample_rate = VOICE_OUTPUT_RATE, .channel = VOICE_CHANNELS,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
    };
    esp_err_t ret = ESP_FAIL;
    ESP_GOTO_ON_FALSE(esp_opus_dec_open(&config, sizeof(config), &audio->decoder) == ESP_AUDIO_ERR_OK,
                      ESP_FAIL, fail, TAG, "open Opus decoder");
    ESP_GOTO_ON_ERROR(open_speaker(audio, volume), fail, TAG, "open speaker");
    return ESP_OK;

fail:
    voice_audio_close(audio);
    return ret;
}

esp_err_t voice_audio_preview(voice_audio_t *audio, unsigned volume)
{
    if (!volume) return ESP_OK;
    esp_err_t err = open_speaker(audio, volume);
    if (err == ESP_OK) {
        // Give the newly enabled DAC and speaker amplifier time to settle.
        vTaskDelay(pdMS_TO_TICKS(PREVIEW_WARMUP_MS));
        const int samples = PREVIEW_BLOCK_SAMPLES * PREVIEW_BLOCKS;
        const int peak = PREVIEW_PERIOD / 4;
        int16_t *pcm = (int16_t *)audio->pcm;
        for (int start = 0; start < samples; start += PREVIEW_BLOCK_SAMPLES) {
            for (int i = 0; i < PREVIEW_BLOCK_SAMPLES; ++i) {
                const int index = start + i;
                const int phase = index % PREVIEW_PERIOD;
                const int wave = peak - abs(phase - PREVIEW_PERIOD / 2);
                int ramp = index < PREVIEW_RAMP ? index : PREVIEW_RAMP;
                if (samples - 1 - index < ramp) ramp = samples - 1 - index;
                pcm[i] = (int16_t)(wave * PREVIEW_AMPLITUDE * ramp / (peak * PREVIEW_RAMP));
            }
            if (esp_codec_dev_write(audio->device, pcm, PREVIEW_BLOCK_SAMPLES * sizeof(*pcm)) != ESP_CODEC_DEV_OK) {
                err = ESP_FAIL;
                break;
            }
        }
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(PREVIEW_DRAIN_MS));
            ESP_LOGI(TAG, "Volume preview written at %u%%", volume);
        }
    }
    voice_audio_close(audio);
    return err;
}

esp_err_t voice_audio_play(voice_audio_t *audio, uint8_t *packet, size_t size)
{
    esp_audio_dec_in_raw_t input = {.buffer = packet, .len = size};
    esp_audio_dec_out_frame_t output = {.buffer = audio->pcm, .len = PCM_BYTES};
    esp_audio_dec_info_t info = {0};
    if (esp_opus_dec_decode(audio->decoder, &input, &output, &info) != ESP_AUDIO_ERR_OK || !output.decoded_size) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return esp_codec_dev_write(audio->device, audio->pcm, output.decoded_size) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

void voice_audio_close(voice_audio_t *audio)
{
    if (audio->device) {
        int result = esp_codec_dev_close(audio->device);
        if (result != ESP_CODEC_DEV_OK) ESP_LOGW(TAG, "close codec: %d", result);
    }
    if (audio->encoder) esp_opus_enc_close(audio->encoder);
    if (audio->decoder) esp_opus_dec_close(audio->decoder);
    free(audio->encoded);
    free(audio->pcm);
    memset(audio, 0, sizeof(*audio));
}
