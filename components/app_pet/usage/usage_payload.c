// SPDX-License-Identifier: Apache-2.0
#include "usage_payload.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const usage_snapshot_t USAGE_SNAPSHOT_EMPTY = {
    .codex = { .ring_percent = -1, .bar_percent = -1 },
};

static void copy_text(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t n = src_len < dst_size - 1 ? src_len : dst_size - 1;
    /* 不在 UTF-8 多字节字符中间截断：回退到字符边界 */
    while (n > 0 && n < src_len && (src[n] & 0xC0) == 0x80) {
        --n;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

usage_band_t usage_band_of(int remaining_percent)
{
    if (remaining_percent < 0) {
        return USAGE_BAND_GREEN;
    }
    if (remaining_percent <= 10) {
        return USAGE_BAND_RED;
    }
    if (remaining_percent <= 30) {
        return USAGE_BAND_AMBER;
    }
    return USAGE_BAND_GREEN;
}

static int parse_percent(const char *value, size_t len)
{
    char buf[8];
    if (len == 0 || len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, value, len);
    buf[len] = '\0';
    char *endp = NULL;
    long v = strtol(buf, &endp, 10);
    /* 必须整段都是十进制数字（"50x"、"abc" 一律当作缺失）。 */
    if (*endp != '\0' || v < 0) {
        return -1;
    }
    return v > 100 ? 100 : (int)v;
}

static void apply_field(usage_vendor_t *v, const char *key, size_t key_len,
                        const char *val, size_t val_len)
{
    if (key_len == 2 && memcmp(key, "ok", 2) == 0) {
        v->ok = val_len == 1 && val[0] == '1';
    } else if (key_len == 14 && memcmp(key, "quota_disabled", 14) == 0) {
        v->quota_disabled = val_len == 1 && val[0] == '1';
    } else if (key_len == 6 && memcmp(key, "reason", 6) == 0) {
        copy_text(v->reason, sizeof(v->reason), val, val_len);
    } else if (key_len == 4 && memcmp(key, "ring", 4) == 0) {
        v->ring_percent = parse_percent(val, val_len);
    } else if (key_len == 10 && memcmp(key, "ring_label", 10) == 0) {
        copy_text(v->ring_label, sizeof(v->ring_label), val, val_len);
    } else if (key_len == 3 && memcmp(key, "bar", 3) == 0) {
        v->bar_percent = parse_percent(val, val_len);
    } else if (key_len == 9 && memcmp(key, "bar_label", 9) == 0) {
        copy_text(v->bar_label, sizeof(v->bar_label), val, val_len);
    } else if (key_len == 9 && memcmp(key, "bar_reset", 9) == 0) {
        copy_text(v->bar_reset, sizeof(v->bar_reset), val, val_len);
    } else if (key_len == 6 && memcmp(key, "tokens", 6) == 0) {
        copy_text(v->tokens_text, sizeof(v->tokens_text), val, val_len);
    }
}

bool usage_payload_parse(const char *text, size_t length, usage_snapshot_t *out)
{
    if (!text || !out || length < 3 || memcmp(text, "v=1", 3) != 0) {
        return false;
    }
    /* 首行必须恰为 "v=1"：后面只能是输入结束、'\n' 或 '\r'（拒绝 "v=10"、"v=1x"）。 */
    if (length > 3 && text[3] != '\n' && text[3] != '\r') {
        return false;
    }
    *out = USAGE_SNAPSHOT_EMPTY;

    const char *p = text;
    const char *end = text + length;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;
        const char *eq = memchr(p, '=', (size_t)(line_end - p));
        if (eq) {
            const char *key = p;
            size_t key_len = (size_t)(eq - p);
            const char *val = eq + 1;
            size_t val_len = (size_t)(line_end - val);
            if (val_len && val[val_len - 1] == '\r') {
                --val_len;
            }
            if (key_len == 7 && memcmp(key, "updated", 7) == 0) {
                copy_text(out->updated_text, sizeof(out->updated_text), val, val_len);
            } else if (key_len == 5 && memcmp(key, "stale", 5) == 0) {
                out->stale = val_len == 1 && val[0] == '1';
            } else if (key_len > 2 && key[0] == 'r' && key[1] == '.') {
                /* Legacy l.* fields are ignored, including in existing NVS records. */
                apply_field(&out->codex, key + 2, key_len - 2, val, val_len);
            }
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    return true;
}

static bool merge_vendor(usage_vendor_t *current, const usage_vendor_t *incoming)
{
    bool reused = false;
    if (incoming->ok || incoming->quota_disabled || incoming->ring_percent >= 0 || current->ring_percent < 0) {
        current->ring_percent = incoming->ring_percent;
        memcpy(current->ring_label, incoming->ring_label, sizeof(current->ring_label));
        current->bar_percent = incoming->bar_percent;
        memcpy(current->bar_label, incoming->bar_label, sizeof(current->bar_label));
        memcpy(current->bar_reset, incoming->bar_reset, sizeof(current->bar_reset));
    } else {
        reused = true;
    }
    if (incoming->ok || incoming->tokens_text[0] || !current->tokens_text[0]) {
        memcpy(current->tokens_text, incoming->tokens_text, sizeof(current->tokens_text));
    } else {
        reused = true;
    }
    current->ok = incoming->ok;
    current->quota_disabled = incoming->quota_disabled;
    memcpy(current->reason, incoming->reason, sizeof(current->reason));
    return reused;
}

void usage_snapshot_merge(usage_snapshot_t *current, const usage_snapshot_t *incoming)
{
    const bool reused = merge_vendor(&current->codex, &incoming->codex);
    if (!reused) {
        memcpy(current->updated_text, incoming->updated_text, sizeof(current->updated_text));
    }
    current->stale = incoming->stale || reused;
}

bool usage_snapshot_has_data(const usage_snapshot_t *snapshot)
{
    return snapshot->codex.ring_percent >= 0 ||
           snapshot->codex.bar_percent >= 0 || snapshot->codex.tokens_text[0];
}

size_t usage_payload_encode(const usage_snapshot_t *snapshot, char *out, size_t capacity)
{
    if (!snapshot || !out || !capacity) {
        return 0;
    }
    const usage_vendor_t *r = &snapshot->codex;
    const int length = snprintf(out, capacity,
        "v=1\nupdated=%s\nstale=%d\n"
        "r.ok=%d\nr.quota_disabled=%d\nr.reason=%s\nr.ring=%d\nr.ring_label=%s\n"
        "r.bar=%d\nr.bar_label=%s\nr.bar_reset=%s\nr.tokens=%s",
        snapshot->updated_text, snapshot->stale ? 1 : 0,
        r->ok ? 1 : 0, r->quota_disabled ? 1 : 0, r->reason, r->ring_percent, r->ring_label,
        r->bar_percent, r->bar_label, r->bar_reset, r->tokens_text);
    return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
}
