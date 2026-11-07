# Source publication checks — 2026-11-07

The publication combines the Codex-only second screen and concurrent desktop
task recognition. An independent review found no remaining functional blocker.

Fresh verification passed: 44 host Python tests, 35 usage collector tests, a
native build and all 4 C tests. `git diff --check` passed. The physical board
continued working through all 10 samples after the real Codex turn ended while
ChatGPT remained active; see the [hardware record](evidence/parallel-activity-2026-11-07.md).

The template's full `pre_commit run --all-files` was also executed. It is not
green: the existing 10 MiB font exceeds its generic 5 MiB limit, the existing
Python entry points lack executable mode bits, the official Python ZIP checksum
is flagged as high-entropy text, and formatting differs from the template rules.
AStyle also defaults to GBK on this Windows installation and cannot decode some
UTF-8 sources without UTF-8 mode. The formatters' broad changes were reverted;
this publication preserves the reviewed and hardware-tested implementation.
No new credentials or runtime data are included. YAML/JSON/TOML validation,
Ruff lint, private-key detection, merge-conflict checks, artifact policy and
component policy all passed.

The tested Windows EXE and firmware bundle remain local generated artifacts.
Release attachment upload was not completed: browser control stopped because
it could not establish the current Edge URL for its policy check. No release
download is advertised until those assets have actually been published.
