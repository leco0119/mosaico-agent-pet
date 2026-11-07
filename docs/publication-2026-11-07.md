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

The tested Windows EXE and firmware bundle were published in
[v2026.11.07](https://github.com/leco0119/mosaico-agent-pet/releases/tag/v2026.11.07)
at 2026-11-07 16:45 UTC+08. The release tag points to source commit
`0fb8afeab14952fcae1f5287b41fe949c492d9d3`. Download links are also in both root
READMEs and the Windows helper guide.

GitHub reports all three attachments as uploaded. Their remote sizes and SHA-256
digests were checked against the local files:

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `MosaicoAgentPet.exe` | 19,068,928 | `16a1a1795a6b2267b7fa6505288ad427987d3d4bc21b59bb4987e88596d755ea` |
| `Agent_Pet-system-update.irisfw` | 3,842,011 | `f291153e3d5ed62f9f492c5753c18f2cbfdd80c0b21bd98c30d5927c207b84c1` |
| `SHA256SUMS.txt` | 183 | `9e6a8681d598f5b4f476419f139c7aae74e2832988363b7b078e300b9b77f44b` |

The two binaries are generated release assets; local copies remain excluded
from the Git source tree.
