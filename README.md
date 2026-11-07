# Mosaico Agent Pet

[简体中文](README_CN.md)

A desk pet for ESP-Mosaico that combines concurrent Codex, ChatGPT desktop,
WorkBuddy, Claude Code and PandaCode activity. Working tasks take precedence
over completed tasks. It also supports XiaoZhi voice,
Wi-Fi selection and volume control on the board.

The second screen is the Codex usage dashboard: swipe left from the pet, or tap
`1/2 ›`. It shows **remaining** quota, reset time and official lifetime tokens.
Swipe right or tap `‹ 桌宠` to return. Wi-Fi and volume pages keep their existing
controls. Pet activity continues to sync while the dashboard is visible.

The same Windows helper collects account usage on startup, reconnect, a screen
refresh tap and every five minutes. Its independent account worker cannot block
pet heartbeats. Boot and connection restore the last saved values first; failed
reads preserve values and their original timestamp as historical data.
On a PC response timeout, the footer displays `PC 未响应 · 历史数据`; the
stored values and timestamp remain unchanged.

See [Windows helper packaging](tools/windows/README.md) for a standalone EXE build.

## Workspace setup

Clone the [ESP-Mosaico workspace](https://github.com/esp-mosaico/esp-mosaico-vibe)
and place this repository at `projects/Agent_Pet`. The firmware identifies itself
as `Agent_Pet` on the board and in Vibe Mode.

```sh
git clone https://github.com/esp-mosaico/esp-mosaico-vibe.git
cd esp-mosaico-vibe
git checkout 1872fd88c01d7807556d01fd2a19864e40a307c9
git submodule update --init submodule/esp-mosaico-utils submodule/esp-mosaico-bsp
git clone https://github.com/leco0119/mosaico-agent-pet.git projects/Agent_Pet
git -C submodule/esp-mosaico-bsp apply ../../projects/Agent_Pet/patches/bsp-psram-display.patch
git -C submodule/esp-mosaico-utils apply ../../projects/Agent_Pet/patches/utils-windowless-gateway.patch
```

The display patch enables direct PSRAM transfers. The Gateway patch keeps
Windows transport processes hidden. Use ESP-IDF
`7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` for the `esp32s31` target.
Follow the workspace [CLI guide](https://github.com/esp-mosaico/esp-mosaico-vibe/blob/1872fd88c01d7807556d01fd2a19864e40a307c9/docs/mosaico-cli_CN.md)
to select the installation method.

```sh
idf.py -C projects/Agent_Pet build
python mosaico.py project sim --project projects/Agent_Pet --headless --duration 3
python -m unittest discover -s projects/Agent_Pet/tools/tests -v
```

## Windows sync

The [single-file helper](tools/windows/README.md) bundles its own runtime and
automatically selects the sole Mosaico attached by USB. It runs in the tray
and can start at Windows sign-in. For development, run
`python projects/Agent_Pet/tools/agent_pet.py`. When several boards are attached,
pass the intended board's full Device ID with `--device-id`.

For usage collection, Codex must be installed and signed in; its window may be closed. The helper uses
the official app-server account read methods. No separate usage_panel watcher
is needed. Display-only cache lives at `%LOCALAPPDATA%/MosaicoAgentPet/usage.json`
and survives EXE updates. A valid old `mosaico-usage/usage.json` is migrated on
first use. No credentials or raw account responses are cached.

ChatGPT task activity requires the Codex desktop app to remain running; its
window can be minimized. The helper reads the app's live task list every three
seconds, combines it with all local session logs, and keeps working while any
task is active. A short status-read failure retains the last state for up to
15 seconds. Closing the app clears its live task source. This desktop-local
interface is version dependent; local log detection remains available if it
changes. See the [parallel activity evidence](docs/evidence/parallel-activity-2026-11-07.md).

## Layout

- `components/app_pet`: portable pet/usage controllers, two GSP scenes, shared
  event routing, pet Iris adapter, and native simulator adapter.
- `components/app_usage_link`: usage RPC delivery, refresh counter and retained NVS cache.
- `components/app_wifi` owns network scanning, connection and the recent network store.
- `components/app_display`: product display/touch adapter.
- [`components/app_voice`](components/app_voice/README.md) owns XiaoZhi enrollment and AI-key voice turns.
- `main`: platform startup and dependency composition.
- `tools/agent_pet.py`: session-log classification and concurrent activity selection.
- `tools/desktop_activity.py`: read-only live task status from the Windows desktop app.
- `tools/pet_runner.py`: single instance, USB reconnects and heartbeats.
- `tools/host_runtime.py`: hidden child processes and public CLI transport.
- `tools/usage_sync.py` and `tools/usage_agent`: background account collection,
  persistent display cache and usage delivery through the existing pet connection.
- `tools/install-agent-pet.ps1`: current-user login shortcut and start/stop/status.

See [integration verification](docs/evidence/codex-usage/README.md) for test results,
simulator captures and completed hardware acceptance.
