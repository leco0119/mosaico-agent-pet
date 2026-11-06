# Mosaico Agent Pet

A desk pet for ESP-Mosaico that displays the most recently active Codex,
WorkBuddy, Claude Code or PandaCode session. It also supports XiaoZhi voice,
Wi-Fi selection and volume control on the board.

## Workspace setup

Clone the [ESP-Mosaico workspace](https://github.com/esp-mosaico/esp-mosaico-vibe)
and place this repository at `projects/my_app`. The directory name matches the
firmware identity and the Windows helper's project selection.

```sh
git clone https://github.com/esp-mosaico/esp-mosaico-vibe.git
cd esp-mosaico-vibe
git checkout 1872fd88c01d7807556d01fd2a19864e40a307c9
git submodule update --init submodule/esp-mosaico-utils submodule/esp-mosaico-bsp
git clone https://github.com/leco0119/mosaico-agent-pet.git projects/my_app
git -C submodule/esp-mosaico-bsp apply ../../projects/my_app/patches/bsp-psram-display.patch
git -C submodule/esp-mosaico-utils apply ../../projects/my_app/patches/utils-windowless-gateway.patch
```

The display patch enables direct PSRAM transfers. The Gateway patch keeps
Windows transport processes hidden. Use ESP-IDF
`7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` for the `esp32s31` target.
Follow the workspace [CLI guide](https://github.com/esp-mosaico/esp-mosaico-vibe/blob/1872fd88c01d7807556d01fd2a19864e40a307c9/docs/mosaico-cli_CN.md)
to select the installation method.

```sh
idf.py -C projects/my_app build
python mosaico.py project sim --project projects/my_app --headless --duration 3
python -m unittest discover -s projects/my_app/tools/tests -v
```

## Windows sync

The [single-file helper](tools/windows/README.md) bundles its own runtime and
automatically selects the sole Mosaico attached by USB. It runs in the tray
and can start at Windows sign-in. For development, run
`python projects/my_app/tools/agent_pet.py`. When several boards are attached,
pass the intended board's full Device ID with `--device-id`.

## Layout

- `components/app_pet`: portable C controller, authored GSP scene, Iris mailbox
  adapter, and native simulator adapter.
- `components/app_wifi` owns network scanning, connection and the recent network store.
- `components/app_display`: product display/touch adapter.
- [`components/app_voice`](components/app_voice/README.md) owns XiaoZhi enrollment and AI-key voice turns.
- `main`: platform startup and dependency composition.
- `tools/agent_pet.py`: session-log classification.
- `tools/pet_runner.py`: single instance, USB reconnects and heartbeats.
- `tools/host_runtime.py`: hidden child processes and public CLI transport.
- `tools/install-agent-pet.ps1`: current-user login shortcut and start/stop/status.
