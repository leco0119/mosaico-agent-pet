# app_pet

Owns both portable UI controllers and the deployable 480x480 RGB565 GSP bundle.
Scene order: ui/main.json (pet/settings, ID 0), ui/usage.json (Codex, ID 1).
pet_pages is the sole event callback owner; scene-qualified handlers keep raw
callback IDs from crossing pages. Horizontal swipes and header buttons switch
main screens; settings disable swipes until returning home. Existing Wi-Fi,
volume and Xiaozhi services remain in app_wifi/app_voice.

app_pet stores current host and voice views; usage/app_usage stores the merged
usage snapshot. Hidden controllers update memory only. Scene entry redraws the
latest state because GSP object operations address the active scene. Background
updates never navigate. The usage controller starts inactive. Native adapters
compile the same controllers and scenes as the firmware.

## Ownership and lifecycle

Main creates both controllers, starts app_usage_link, then calls
status_link_start_with_usage(pet, ui, voice, wifi, usage, read_refresh, ctx).
The transport owns settings, the router and its 50 ms dispatch timer. UI state
mutation after startup belongs to the GSP task. Iris handlers copy bounded
payloads into a mutex-protected mailbox, and read a published diagnostic copy.
read_refresh is a thread-safe callback supplied by main, avoiding a dependency
cycle. The original pet-only status_link_start entry point remains available.

Device controllers/timers/callbacks live until reboot. Native shutdown quiesces
dispatch, detaches pet_pages and destroys the controllers before closing GSP.
No API is ISR-safe. Create clears output on failure; destroy preserves ownership
when required timer/callback removal fails. Controller APIs are UI-task-only,
except creation before dispatch and destruction after dispatch is quiescent.

## Pet protocol

Service 0x5354, method 1: UTF-8 lines T=Codex, A=1, P=运行命令, D=exec_command.
Animations: 0 idle, 1 working, 2 waiting, 3 done. Payload maximum 512 bytes;
lines shorter than 128 bytes including key. Unknown/invalid fields are ignored.
Latest queued status wins. Valid updates renew a 30-second heartbeat. SET reply
acknowledges queueing and includes usage_protocol=1 / usage_refresh when enabled.
Method 2 returns animation, connection, update/error, voice/Wi-Fi diagnostics
and the same usage fields. Both replies include the Iris boot ID as a lossless
string, so the host resends cache even after a reboot between successful
heartbeats. Existing clients may ignore the added fields.

## Usage UI

usage_payload parses/merges v1 text. Only r.* Codex fields are displayed; old
l.* fields are ignored for storage compatibility. Failed missing values retain
previous numbers and time. Valid zero and successful absence replace old values.
Transport owns merging; app_usage_apply accepts the authoritative snapshot.
Bands: 0..10 red, 11..30 amber, 31..100 green; -1 means unavailable. Refresh
increments the app_usage_link counter and starts a 30-second UI timeout.
Failures/timeouts preserve numbers; repeated taps while updating coalesce.

## Verification

Native tests: tests/native. Python: tools/tests and tools/usage_agent/tests.
tools/tests/capture_two_screens.py drives real native taps/swipes and captures
refresh, failure, timeout, return and settings flows. Simulator values are
fixtures, not account data. MOSAICO_PET_SIM_CYCLE=loop supplies pet activity.
USAGE_PANEL_OFFLINE / USAGE_PANEL_EMPTY exercise saved/no-data startup. Host
capture mode fences completed draws before recording operation completion.
