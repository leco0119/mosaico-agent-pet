#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_usage.h"

static void owner_callback(esp_gsp_handle_t ui, const esp_gsp_event_t *event, void *ctx)
{
    (void)ui; (void)event; (void)ctx;
}

int main(void)
{
    struct fake_ui ui = {0};
    ui.event_cb = owner_callback;
    app_usage_handle_t controller = NULL;
    assert(app_usage_create(&ui, &controller) == ESP_GSP_OK);
    assert(ui.event_cb == owner_callback); /* the combined UI owns event routing */
    assert(ui.r_pct[0] == '\0'); /* usage starts off-screen; no inactive-scene writes */
    assert(app_usage_set_active(controller, true) == ESP_GSP_OK);
    assert(strcmp(ui.r_pct, "—") == 0);
    usage_snapshot_t canonical = USAGE_SNAPSHOT_EMPTY;
    usage_snapshot_t incoming;
    const char good[] = "v=1\nupdated=更新 21:58\nl.ok=1\nl.ring=62\nl.tokens=1.24B\nr.ok=1\nr.ring=3\nr.ring_label=每周 · 周四 07:25 重置\nr.tokens=5.15B";
    assert(usage_payload_parse(good, strlen(good), &canonical));
    assert(app_usage_apply(controller, &canonical) == ESP_GSP_OK);
    assert(strcmp(ui.r_pct, "3%") == 0);
    assert(strcmp(ui.r_window, "每周额度") == 0);
    assert(strcmp(ui.r_reset, "周四 07:25 重置") == 0);
    assert(!ui.r_bar_visible);
    incoming = canonical;
    incoming.codex.bar_percent = 22;
    assert(app_usage_apply(controller, &incoming) == ESP_GSP_OK);
    assert(ui.r_bar_visible);
    assert(app_usage_apply(controller, &canonical) == ESP_GSP_OK);
    assert(!ui.r_bar_visible);

    /* Two RPC observations coalesced before the display tick: a successful
     * quota removal followed by a failed quota read with a successful total. */
    const char clear[] = "v=1\nupdated=更新 22:00\nl.ok=1\nr.ok=1\nr.tokens=5.16B";
    assert(usage_payload_parse(clear, strlen(clear), &incoming));
    usage_snapshot_merge(&canonical, &incoming);
    const char partial[] = "v=1\nupdated=更新 22:01\nl.ok=0\nr.ok=0\nr.tokens=5.17B";
    assert(usage_payload_parse(partial, strlen(partial), &incoming));
    usage_snapshot_merge(&canonical, &incoming);
    assert(app_usage_apply(controller, &canonical) == ESP_GSP_OK);
    assert(strcmp(ui.r_pct, "—") == 0);
    assert(strcmp(ui.r_tokens, "5.17B") == 0);

    assert(usage_payload_parse(good, strlen(good), &canonical));
    canonical.stale = true;
    assert(app_usage_apply(controller, &canonical) == ESP_GSP_OK);
    assert(strstr(ui.status, "历史数据") != NULL);
    const esp_gsp_event_t click = {.refresh = true, .scene_id = 1};
    app_usage_handle_event(controller, &click);
    assert(strstr(ui.action, "更新中") != NULL);
    assert(strcmp(ui.r_pct, "3%") == 0);
    assert(ui.timer_cb != NULL);
    ui.timer_cb(&ui, ui.timer_ctx);
    assert(strstr(ui.status, "PC 未响应") != NULL && strstr(ui.status, "历史数据") != NULL);
    assert(strcmp(ui.r_pct, "3%") == 0 && strcmp(ui.r_tokens, "5.15B") == 0);
    assert(app_usage_set_active(controller, false) == ESP_GSP_OK);
    canonical.codex.ring_percent = 88;
    assert(app_usage_apply(controller, &canonical) == ESP_GSP_OK);
    assert(strcmp(ui.r_pct, "3%") == 0);
    assert(app_usage_set_active(controller, true) == ESP_GSP_OK);
    assert(strcmp(ui.r_pct, "88%") == 0);
    assert(app_usage_destroy(&controller) == ESP_GSP_OK && controller == NULL);
    assert(ui.event_cb == owner_callback);
    puts("usage_controller: authoritative handoff and offline timeout ok");
    return 0;
}
