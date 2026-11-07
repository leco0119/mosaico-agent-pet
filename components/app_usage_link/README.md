# app_usage_link

Owns Codex usage transport and persistence. Depends on app_pet's public usage
controller/payload API, esp-gsp, ESP-Iris, FreeRTOS and NVS. Main calls
`app_usage_link_start(ui, usage)` once after NVS and app_usage_create. It supplies
`app_usage_link_refresh_sequence` to status_link, avoiding a circular dependency.

Service `0x5550`, method `1` accepts usage_payload v1 text and replies `ok=1`.
The serialized Iris handler owns the canonical merged snapshot and reusable
storage buffer. A mutex protects the pending handoff; a 250 ms GSP timer copies
it into an instance buffer and applies it on the UI task. No large task-stack
buffers or account calls are used on-device. A refreshed snapshot may replace
an unapplied one, after merging against the canonical state.

NVS namespace/key `usage_panel/snapshot_v1` retain migration compatibility. Boot
restores cached values as historical. No-data failures never erase a valid
record. Save failures are logged and leave RAM values usable. Writes happen
outside the UI mailbox lock on the Iris task. Snapshot/time retention is tested
with fake NVS and the real payload/merge code.

The refresh counter is atomic, incremented by the UI callback and read by pet
RPC handlers. The PC polls it through its existing selected USB client, with no
second watcher or log follower. The singleton and its timer live until reboot;
there is no stop API. Start rejects duplicate initialization and returns errors
for invalid handles, failed registration or allocation. The composition root
aborts on startup failure. Controllers/callbacks remain alive until reboot.
