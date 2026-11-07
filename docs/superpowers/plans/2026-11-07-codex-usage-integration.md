# Codex Usage Integration Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Ship Agent Pet with a second Codex usage screen and one Windows helper.

**Architecture:** app_pet owns both portable UI controllers and the multi-scene
bundle. A new app_usage_link owns usage RPC/NVS. The PC runner owns the one
MosaicoClient; a background UsageSync worker only collects/caches account data.

**Tech Stack:** C, ESP-IDF, GSP native bridge, Python stdlib, C# Windows launcher.

**Spec:** ../specs/2026-11-07-codex-usage-integration.md

## Global Constraints

- ESP-IDF 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe; GSP 1.5.1/GSPC 0.6.1.
- 480x480 RGB565; retain partition table and credentials.
- No sibling usage_panel dependency and no Claude usage collection.
- Device operations use mosaico.py; shared native simulator first.

## Review Focus

- Slow account read must not delay pet heartbeat or hold its USB client.
- Settings events and usage refresh may share raw action IDs; route by scene.
- Startup or EXE upgrade with a failed read must retain the last usable data.
- Reconnect/reboot must resend cache and discover refresh capability safely.
- Failure of the usage service alone must not mark the pet USB disconnected.

### Task 1: PC collector and shared transport

**Files:** tools/usage_agent/{codex_rpc,codex_source,payload,snapshot,textfmt}.py,
tools/usage_sync.py, tools/{pet_runner,host_runtime}.py, tools/tests/test_usage_sync.py.

**Interfaces:** UsageSync.start(), request(), peek() -> (revision, document),
close(); UsageDelivery.observe(reply), tick(client, now), disconnected().
MosaicoClient.rpc(method, payload='', *, service='0x5354').

- [x] Add behavior tests for cached startup, slow/coalesced reads, partial failure,
  migration, reconnect delivery, manual refresh and old firmware capability.
- [x] Run new tests and record missing implementation failures.
- [x] Migrate the tested account parser and implement worker/delivery ownership.
- [x] Integrate runner; run Python suites, expecting all tests to pass.

### Task 2: Two-screen native UI and device persistence

**Files:** components/app_pet/{usage/,include/,pet_pages.c,pet_settings.c,
status_link.c,ui/usage.json,host/}, components/app_usage_link/, main/.

**Interfaces:** app_usage_handle_event(handle,event),
pet_settings_handle_event(handle,event), pet_pages_create(ui,settings,pet,usage,out).
Usage RPC 0x5550/1 accepts v1 text; pet heartbeat replies include usage_refresh
and usage_protocol=1. Existing legacy clients can ignore additional fields.

- [x] Migrate native retention tests and add navigation/event ownership tests.
- [x] Run the new cases before implementing the changed controller contracts.
- [x] Compose pet and usage as two scenes with one event dispatcher; disable
  scene swipes in settings; preserve voice and background updates.
- [x] Add usage mailbox/NVS and refresh counter, composed by main.
- [x] Run native tests, build shared backend, verify real swipes/settings/refresh,
  failures/timeouts and off-screen updates with captures.

### Task 3: Firmware, Windows package and delivery documentation

**Files:** tools/windows/{build.py,portable_agent.py,README.md}, README{,_CN}.md,
component READMEs, docs/evidence/codex-usage/.

- [x] Include the usage package and worker in the EXE payload. Keep the old
  local EXE as a backup before replacing the release artifact.
- [x] Verify pinned SDK, run build doctor and full firmware build.
- [x] Build self-contained EXE and run isolated self-test without a device write.
- [x] Review whole change with a fresh reviewer, resolve material findings,
  record verification and provide artifact paths and remaining device checks.
