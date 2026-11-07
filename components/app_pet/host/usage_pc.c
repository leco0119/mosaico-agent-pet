// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_usage.h"
#include "usage_pc.h"

/* Shared native controller. Click cycle: dual-window, failed read, timeout,
 * then recovered single-window. Host-only environment can supply other payloads. */
#define FEED_DELAY_MS 1000
#define SAMPLE_CYCLE 4

static struct {
    app_usage_handle_t controller;
    esp_gsp_handle_t ui;
    void *timer;
    unsigned clicks;
    bool offline;
    usage_snapshot_t current;
    char payload[USAGE_PAYLOAD_MAX];
} backend;

static const char SAMPLE_A[] =
    "v=1\n"
    "updated=更新 21:58\n"
    "r.ok=1\n"
    "r.reason=\n"
    "r.ring=2\n"
    "r.ring_label=每周 · 周四 07:25 重置\n"
    "r.bar=-1\n"
    "r.bar_label=\n"
    "r.bar_reset=\n"
    "r.tokens=5.32B";
static const char SAMPLE_B[] =
    "v=1\nupdated=更新 22:00\nr.ok=1\nr.ring=76\n"
    "r.ring_label=5 小时 · 2h14m 后重置\nr.bar=22\nr.bar_label=每周\n"
    "r.bar_reset=周四 07:25 重置\nr.tokens=5.32B";
static const char SAMPLE_FAILED[] =
    "v=1\n"
    "updated=更新 22:03\n"
    "r.ok=0\n"
    "r.reason=读取失败\n"
    "r.ring=-1\n"
    "r.ring_label=\n"
    "r.bar=-1\n"
    "r.bar_label=\n"
    "r.bar_reset=\n"
    "r.tokens=";

static void feed(esp_gsp_handle_t ui, void *ctx)
{
    const char *sample = ctx;
    if (backend.timer) {
        esp_gsp_timer_delete(ui, backend.timer);
        backend.timer = NULL;
    }
    /* 栈上快照仅限 PC 模拟器；设备端由 RPC 组件持有实例缓冲。 */
    usage_snapshot_t snap;
    if (usage_payload_parse(sample, strlen(sample), &snap)) {
        snap.stale = backend.offline;
        usage_snapshot_merge(&backend.current, &snap);
        esp_gsp_err_t err = app_usage_apply(backend.controller, &backend.current);
        if (err == ESP_GSP_OK && getenv("USAGE_PANEL_CAPTURE")) {
            err = esp_gsp_flush(ui, 1000);
        }
        fprintf(stderr, "usage_backend: applied clicks=%u stale=%d error=%d\n",
                backend.clicks, backend.current.stale, (int)err);
        fflush(stderr);
    }
}

/* 一次只保留一个待喂定时器：先删旧的，再建新的。 */
static void arm_feed(const char *sample)
{
    if (backend.timer) {
        esp_gsp_timer_delete(backend.ui, backend.timer);
        backend.timer = NULL;
    }
    backend.timer = esp_gsp_timer_create(backend.ui, FEED_DELAY_MS, feed, (void *)sample);
    if (!backend.timer) {
        fprintf(stderr, "usage_backend: feed timer create failed\n");
    }
}

static void on_refresh(void *ctx)
{
    (void)ctx;
    ++backend.clicks;
    fprintf(stderr, "usage_backend: refresh click %u\n", backend.clicks);
    unsigned stage = backend.clicks % SAMPLE_CYCLE;
    if (backend.offline || stage == 3) {
        return;   /* 模拟 PC 无响应 */
    }
    arm_feed(stage == 1 ? SAMPLE_B : stage == 2 ? SAMPLE_FAILED : SAMPLE_A);
}

esp_gsp_err_t usage_pc_start(esp_gsp_handle_t ui, app_usage_handle_t controller)
{
    backend.ui = ui;
    backend.current = USAGE_SNAPSHOT_EMPTY;
    backend.controller = controller;
    app_usage_on_refresh(backend.controller, on_refresh, NULL);
    /* Simulates restored data without a PC. NVS persistence is tested separately. */
    backend.offline = getenv("USAGE_PANEL_OFFLINE") != NULL;
    if (getenv("USAGE_PANEL_EMPTY")) {
        return ESP_GSP_OK;
    }
    /* Windows getenv text is ANSI. Read UTF-8 payload bytes from a file instead. */
    const char *path = getenv("USAGE_PANEL_PAYLOAD_FILE");
    if (path) {
        FILE *file = fopen(path, "rb");
        if (!file) {
            fprintf(stderr, "usage_backend: cannot open sample payload\n");
            return ESP_GSP_ERR_INVALID_ARG;
        }
        size_t size = fread(backend.payload, 1, sizeof(backend.payload) - 1, file);
        bool valid = !ferror(file) && fgetc(file) == EOF && size > 0;
        fclose(file);
        if (!valid) {
            fprintf(stderr, "usage_backend: invalid sample payload size\n");
            return ESP_GSP_ERR_INVALID_ARG;
        }
        backend.payload[size] = '\0';
    }
    arm_feed(path ? backend.payload : SAMPLE_A);
    return backend.timer ? ESP_GSP_OK : ESP_GSP_ERR_NO_MEM;
}

void usage_pc_stop(esp_gsp_handle_t ui)
{
    if (backend.timer) {
        esp_gsp_timer_delete(ui, backend.timer);
        backend.timer = NULL;
    }
    app_usage_on_refresh(backend.controller, NULL, NULL);
}
