# Windows Agent Pet helper

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
