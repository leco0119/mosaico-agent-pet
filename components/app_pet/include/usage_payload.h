// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USAGE_TEXT_MAX 32
#define USAGE_TOKENS_MAX 16
#define USAGE_UPDATED_MAX 24
/* Complete bounded serialization for NVS, including all maximum-length fields. */
#define USAGE_PAYLOAD_MAX 1024

/** 一家的显示数据；百分比 -1 表示该窗口不存在。 */
typedef struct {
    bool ok;
    bool quota_disabled;  /* intentional absence; never restore old quota */
    char reason[USAGE_TEXT_MAX];
    int ring_percent;
    char ring_label[USAGE_TEXT_MAX];
    int bar_percent;
    char bar_label[USAGE_TEXT_MAX];
    char bar_reset[USAGE_TEXT_MAX];
    char tokens_text[USAGE_TOKENS_MAX];
} usage_vendor_t;

typedef struct {
    usage_vendor_t codex;   /* v1 wire prefix r.*, independent of screen position */
    char updated_text[USAGE_UPDATED_MAX];
    bool stale;             /* contains historical data, including boot restore */
} usage_snapshot_t;

/** 全空快照：文本为空、ok=false、所有百分比为 -1。解析和初始渲染的起点。 */
extern const usage_snapshot_t USAGE_SNAPSHOT_EMPTY;

/** 剩余百分比的颜色分段：0..10 红、11..30 橙、31..100 绿。 */
typedef enum {
    USAGE_BAND_RED = 0,
    USAGE_BAND_AMBER = 1,
    USAGE_BAND_GREEN = 2,
} usage_band_t;
#define USAGE_BAND_COUNT 3

/** 剩余百分比 -> 分段。负值（窗口不存在）按绿色处理，调用方自行决定是否显示。 */
usage_band_t usage_band_of(int remaining_percent);

/** 把 "key=value\n..." 行文本解析进 out（先置为 USAGE_SNAPSHOT_EMPTY）。
 * 首行必须恰为 "v=1"；百分比须是纯十进制整数，否则为 -1；接受 CRLF。
 * 返回 false 表示首行不匹配或输入为空。 */
bool usage_payload_parse(const char *text, size_t length, usage_snapshot_t *out);

/** Merge an observation into current. Failed missing fields retain known values
 * and the previous update time; successful absences and real zeros replace them.
 * The two pointers must refer to different snapshots. */
void usage_snapshot_merge(usage_snapshot_t *current, const usage_snapshot_t *incoming);

/** Whether at least one quota or token value is known. */
bool usage_snapshot_has_data(const usage_snapshot_t *snapshot);

/** Encode a complete v1 snapshot for storage; return byte count, or 0 on failure.
 * out requires space for the trailing NUL; the returned count excludes it. */
size_t usage_payload_encode(const usage_snapshot_t *snapshot, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
