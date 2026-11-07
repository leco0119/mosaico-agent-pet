#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "usage_store.h"

int main(void)
{
    usage_store_t store = {0};
    usage_snapshot_t saved = USAGE_SNAPSHOT_EMPTY;
    usage_snapshot_t restored = USAGE_SNAPSHOT_EMPTY;
    assert(usage_store_load(&store, &restored) == ESP_ERR_NVS_NOT_FOUND);
    /* A real old text record survives the in-memory structure migration. */
    nvs_handle_t legacy_handle;
    const char legacy[] = "v=1\nupdated=更新 20:00\nl.ok=1\nl.ring=89\nl.tokens=7.44B\nr.ok=1\nr.ring=2\nr.tokens=5.32B";
    assert(nvs_open("usage_panel", NVS_READWRITE, &legacy_handle) == ESP_OK);
    assert(nvs_set_blob(legacy_handle, "snapshot_v1", legacy, strlen(legacy)) == ESP_OK);
    assert(nvs_commit(legacy_handle) == ESP_OK);
    nvs_close(legacy_handle);
    assert(usage_store_load(&store, &restored) == ESP_OK);
    assert(restored.codex.ring_percent == 2 && strcmp(restored.codex.tokens_text, "5.32B") == 0);
    saved.codex.ok = true;
    saved.codex.ring_percent = 3;
    strcpy(saved.codex.tokens_text, "5.15B");
    strcpy(saved.updated_text, "更新 21:58");
    assert(usage_store_save(&store, &saved) == ESP_OK);

    usage_store_t next_boot = {0};
    assert(usage_store_load(&next_boot, &restored) == ESP_OK);
    assert(restored.codex.ring_percent == 3 && strcmp(restored.codex.tokens_text, "5.15B") == 0);
    assert(strcmp(restored.updated_text, "更新 21:58") == 0);

    fake_nvs_fail_commit(1);
    saved.codex.ring_percent = 9;
    assert(usage_store_save(&store, &saved) == ESP_FAIL);
    assert(usage_store_load(&next_boot, &restored) == ESP_OK);
    assert(restored.codex.ring_percent == 3);  /* last durable data survives a failed write */
    fake_nvs_fail_commit(0);
    assert(usage_store_save(&store, &saved) == ESP_OK);
    assert(usage_store_load(&next_boot, &restored) == ESP_OK);
    assert(restored.codex.ring_percent == 9);

    usage_snapshot_t empty = USAGE_SNAPSHOT_EMPTY;
    assert(usage_store_save(&store, &empty) == ESP_OK);
    assert(usage_store_load(&next_boot, &restored) == ESP_OK);
    assert(restored.codex.ring_percent == 9);  /* failed emptiness retains known data */
    empty.codex.ok = true;
    assert(usage_store_save(&store, &empty) == ESP_OK);
    assert(usage_store_load(&next_boot, &restored) == ESP_OK);
    assert(!usage_snapshot_has_data(&restored));  /* successful removal survives reboot */
    assert(usage_store_save(&store, &saved) == ESP_OK);
    assert(usage_store_load(&next_boot, &restored) == ESP_OK);

    fake_nvs_corrupt();
    assert(usage_store_load(&next_boot, &restored) == ESP_ERR_INVALID_RESPONSE);
    assert(restored.codex.ring_percent == 9);  /* rejected input must not mutate the snapshot */
    assert(usage_store_save(&store, &saved) == ESP_OK);
    assert(usage_store_load(&next_boot, &restored) == ESP_OK);
    puts("usage_store: boot restore and failed-write recovery ok");
    return 0;
}
