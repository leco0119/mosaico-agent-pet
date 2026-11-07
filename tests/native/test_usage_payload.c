// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usage_payload.h"

static const char SAMPLE[] =
    "v=1\nupdated=更新 21:58\n"
    "l.ok=1\nl.ring=89\nl.tokens=7.44B\n"
    "r.ok=1\nr.ring=62\nr.ring_label=5 小时 · 2h14m 后重置\n"
    "r.bar=38\nr.bar_label=每周\nr.bar_reset=周四 09:00 重置\nr.tokens=5.15B";

static void test_retention(void)
{
    usage_snapshot_t current, incoming;
    assert(usage_payload_parse(SAMPLE, strlen(SAMPLE), &current));
    const char failed[] = "v=1\nupdated=更新 22:00\nr.ok=0\nr.reason=读取失败";
    assert(usage_payload_parse(failed, strlen(failed), &incoming));
    usage_snapshot_merge(&current, &incoming);
    assert(current.stale && current.codex.ring_percent == 62 && current.codex.bar_percent == 38);
    assert(strcmp(current.codex.tokens_text, "5.15B") == 0);
    assert(strcmp(current.updated_text, "更新 21:58") == 0);
    assert(!current.codex.ok && strcmp(current.codex.reason, "读取失败") == 0);
    usage_snapshot_merge(&current, &incoming);
    assert(strcmp(current.updated_text, "更新 21:58") == 0);

    strcpy(incoming.codex.tokens_text, "5.16B");
    usage_snapshot_merge(&current, &incoming);
    assert(current.codex.ring_percent == 62 && strcmp(current.codex.tokens_text, "5.16B") == 0);
    incoming.codex.tokens_text[0] = '\0';
    incoming.codex.ring_percent = 0;
    usage_snapshot_merge(&current, &incoming);
    assert(current.codex.ring_percent == 0 && strcmp(current.codex.tokens_text, "5.16B") == 0);

    /* Codex-only refresh after a legacy dual-vendor record must become fresh. */
    const char recovered[] = "v=1\nupdated=更新 22:01\nr.ok=1\nr.ring=0\nr.tokens=0";
    assert(usage_payload_parse(recovered, strlen(recovered), &incoming));
    usage_snapshot_merge(&current, &incoming);
    assert(!current.stale && current.codex.ring_percent == 0 && current.codex.bar_percent == -1);
    assert(strcmp(current.codex.tokens_text, "0") == 0 && strcmp(current.updated_text, "更新 22:01") == 0);

    char encoded[USAGE_PAYLOAD_MAX];  /* Native host stack, not a device task. */
    current.stale = true;
    size_t length = usage_payload_encode(&current, encoded, sizeof(encoded));
    assert(length > 0 && strstr(encoded, "\nl.") == NULL);
    assert(usage_payload_parse(encoded, length, &incoming));
    assert(incoming.stale && incoming.codex.ok && incoming.codex.ring_percent == 0);
    assert(strcmp(incoming.codex.tokens_text, "0") == 0 && strcmp(incoming.updated_text, "更新 22:01") == 0);
    assert(usage_payload_encode(&current, encoded, 5) == 0);

    const char legacy_only[] = "v=1\nl.ok=1\nl.ring=62\nl.tokens=1.24B";
    assert(usage_payload_parse(legacy_only, strlen(legacy_only), &current));
    assert(!usage_snapshot_has_data(&current));
    assert(usage_payload_parse(failed, strlen(failed), &incoming));
    usage_snapshot_merge(&current, &incoming);
    assert(!current.stale && !usage_snapshot_has_data(&current));
}

int main(void)
{
    usage_snapshot_t snap;
    assert(usage_payload_parse(SAMPLE, strlen(SAMPLE), &snap));
    assert(strcmp(snap.updated_text, "更新 21:58") == 0);
    assert(snap.codex.ok && snap.codex.ring_percent == 62 && snap.codex.bar_percent == 38);
    assert(strcmp(snap.codex.ring_label, "5 小时 · 2h14m 后重置") == 0);
    assert(strcmp(snap.codex.tokens_text, "5.15B") == 0);
    assert(!usage_payload_parse("r.ok=1\n", 7, &snap));

    char longline[200] = "v=1\nr.ring_label=";
    memset(longline + strlen(longline), 'x', 100);
    longline[strlen("v=1\nr.ring_label=") + 100] = '\0';
    assert(usage_payload_parse(longline, strlen(longline), &snap));
    assert(strlen(snap.codex.ring_label) == USAGE_TEXT_MAX - 1);
    const char bad[] = "v=1\nr.ring=250\nr.bar=-7\n";
    assert(usage_payload_parse(bad, strlen(bad), &snap));
    assert(snap.codex.ring_percent == 100 && snap.codex.bar_percent == -1);
    const char cjk[] = "v=1\nr.ring_label=一二三四五六日小时后重置\n";
    assert(usage_payload_parse(cjk, strlen(cjk), &snap));
    assert(strlen(snap.codex.ring_label) == 30);
    const char *invalid[] = {"v=1\nr.ring=\n", "v=1\nr.ring=abc\n", "v=1\nr.ring=50x\n"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(usage_payload_parse(invalid[i], strlen(invalid[i]), &snap));
        assert(snap.codex.ring_percent == -1);
    }
    assert(!usage_payload_parse("v=10\nr.ok=1", strlen("v=10\nr.ok=1"), &snap));
    assert(usage_payload_parse("v=1", 3, &snap));
    assert(!snap.codex.ok && snap.codex.ring_percent == -1 && !snap.updated_text[0]);

    char crlf[2 * sizeof(SAMPLE)];
    size_t n = 0;
    for (const char *p = SAMPLE; *p; ++p) {
        if (*p == '\n') { crlf[n++] = '\r'; }
        crlf[n++] = *p;
    }
    crlf[n] = '\0';
    usage_snapshot_t lf;
    assert(usage_payload_parse(SAMPLE, strlen(SAMPLE), &lf));
    assert(usage_payload_parse(crlf, n, &snap));
    assert(strcmp(lf.updated_text, snap.updated_text) == 0 && lf.codex.ok == snap.codex.ok);
    assert(lf.codex.ring_percent == snap.codex.ring_percent && lf.codex.bar_percent == snap.codex.bar_percent);
    assert(strcmp(lf.codex.ring_label, snap.codex.ring_label) == 0);
    assert(strcmp(lf.codex.bar_reset, snap.codex.bar_reset) == 0);
    assert(strcmp(lf.codex.tokens_text, snap.codex.tokens_text) == 0);
    assert(usage_band_of(0) == USAGE_BAND_RED && usage_band_of(10) == USAGE_BAND_RED);
    assert(usage_band_of(11) == USAGE_BAND_AMBER && usage_band_of(30) == USAGE_BAND_AMBER);
    assert(usage_band_of(31) == USAGE_BAND_GREEN && usage_band_of(100) == USAGE_BAND_GREEN);
    assert(usage_band_of(-1) == USAGE_BAND_GREEN);
    test_retention();
    puts("usage_payload: Codex migration, retention and parsing ok");
    return 0;
}
