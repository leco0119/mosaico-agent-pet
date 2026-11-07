# Windows Agent Pet helper

Download the ready-to-run [MosaicoAgentPet.exe](https://github.com/leco0119/mosaico-agent-pet/releases/download/v2026.11.07/MosaicoAgentPet.exe).
The [release page](https://github.com/leco0119/mosaico-agent-pet/releases/tag/v2026.11.07)
also provides matching firmware and SHA-256 checksums.
The EXE is distributed as a release asset, outside the source tree. To rebuild
it, follow the instructions below.

Build on Windows x64 with Python 3.14 and the Windows .NET Framework C# compiler.
The input files are the official Python 3.14.10 x64 embeddable ZIP and the
matching ESP-Iris environment prepared by the workspace CLI. The build checks
the Python ZIP checksum and the Iris requirements fingerprint.

Run the builder from the Mosaico workspace root.

    python projects/Agent_Pet/tools/windows/build.py --python-embed <python-embed.zip> --iris-environment <iris-py3.14-directory> --output <MosaicoAgentPet.exe>

Copy the EXE to the target Windows computer and run it. The launcher stores
the unpacked runtime and logs under the current user's LocalAppData. Its
checkbox enables login startup from the selected EXE path.
The executable, window and tray use `PetIdle.ico`, derived from the idle
artwork in `components/app_pet/ui/pet/pet_0.png`.

The optional developer check uses the bundled runtime and an isolated PATH.
It lists USB candidates through the Mosaico CLI.

    Start-Process -FilePath <MosaicoAgentPet.exe> -ArgumentList '--self-test', '<test-directory>' -Wait

The packaged workspace contains the host transport and product contract.
It uses the public Mosaico CLI for USB selection, live validation and the
existing screen status RPC. Its runtime seed follows the CLI's dependency
fingerprint, and device ownership remains with ESP-Iris Gateway.

The same EXE now bundles `usage_sync.py` and the Codex account reader. Account
queries run on one background worker; only the pet loop uses the USB client.
Refreshes occur on startup, reconnect, screen request and every 300 seconds.
Display cache is `%LOCALAPPDATA%/MosaicoAgentPet/usage.json`, outside versioned
payload directories. The first run migrates a valid legacy `mosaico-usage/usage.json`.
No account secrets/raw replies are stored. Closing the helper cancels its owned
app-server process. Old pet firmware remains usable without usage RPC support.

The packager also accepts the existing portable Iris environment layout
(`Scripts/Lib/site-packages`) when its fingerprint matches exactly. Self-test
imports the usage dependency set and verifies the encoder without querying an
account. Rebuilding the EXE is required after Python source changes.

`desktop_activity.py` reads live Codex/ChatGPT activity through the desktop app's
local app-tools named pipe. It calls only `list_threads`, including pinned chats
and paginated ChatGPT history when the recent list reaches 50 entries. The pipe
is rediscovered after app restarts. The current/most recent local Codex chat ID
provides request attribution; SQLite is opened read-only for that ID, never for
live task state. No authentication material or message content is needed.

Polling is limited to once per three seconds, with a three-second request budget.
Transient failures retain state for at most 15 seconds; a closed app clears it.
Local log parsing still works without the desktop app. The interface was checked
against Codex desktop 26.1103.4239.0 and may require adaptation after app updates.
The standalone self-test also imports this module using the embedded interpreter.
