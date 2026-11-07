# Execution ledger — 2026-11-07-codex-usage-integration.md

Baseline: main 0e2e78a; branch feat/codex-usage-screen. Existing untracked
tools/windows/MosaicoAgentPet.exe belongs to the user and must be backed up.

User approved the concrete proposal and explicitly requested implementation;
continue without another design/plan approval. Execute in this checkout on a
feature branch to preserve workspace-relative SDK/packaging paths. No parallel
implementation edits. Final independent review is required by execution workflow.

Task 1: complete. 29 unittest cases and 35 migrated parser/RPC pytest cases pass.
Slow reads, migration, failed reads/writes, reconnect delivery, manual refresh,
legacy firmware, account cancellation and packaged source inventory covered.

Task 2: complete, including normal/offline/empty simulator interaction evidence.
Four native C cases pass. Native simulator exposed active-scene-only GSP setters;
controllers now cache hidden state and repaint when entering their scene. This
is required by the SDK, not a change to the approved interaction design.

Task 3: complete. Final EXE build and isolated self-test pass (Python 3.14.10).
The packaged account reader also successfully queried real official account data.
Firmware SDK verified at the pinned SHA; doctor and final firmware build passed.
Initial full build found that the repo-required BSP PSRAM patch had not been
installed. Applied both existing setup patches after clean-submodule/check
verification. Final firmware log: 20261107-150936-build-52856/raw.log under
.codex-runs/idf-low-noise-build. The final EXE was copied to tools/windows,
with the original preserved under .codex-runs/windows-build before replacement.

Device discovery found one board (serial 1C2904D09154). Claim was refused because
the existing desktop helper's separate project Gateway holds the endpoint. No
force, kill or device write performed. A subsequent public read through the
existing helper's Gateway confirmed the board is in Vibe Mode. Keep its ownership
until coordinated; device installation and live hardware acceptance are separate.

Final independent review found one P2: a quick board reboot with refresh counter
still zero could evade reconnect detection. New regression reproduced 1 rather
than 2 collections. Pet replies now carry the Iris boot ID as a lossless string;
UsageDelivery invalidates the sent revision and requests collection on boot
change even without a USB-disconnected event. Targeted RED/GREEN and the full
Python suite passed; final firmware and EXE include the fix. The independent
review has no remaining material code findings. Hardware acceptance remains
explicitly unperformed. Detailed checks and screenshots: docs/evidence/codex-usage.

The implementation stays in this feature branch for review, without commits,
push, merge, device installation or replacement of the user's running helper.

Delivery addition: built build/Agent_Pet-system-update.irisfw and inspected its
three components successfully. A first packaging-only failure came from using
the helper's isolated embedded interpreter (script import path omitted); using
the existing normal Iris Python 3.12 environment resolved it without source edits.
Final log: .codex-runs/system-update-bundle-final.log. The package includes the
application and both UI scenes, retaining the partition layout and excluding NVS.

Follow-up hardware request: installed the complete bundle through the existing
Gateway, operation 515c6a8a-0448-4fdc-b8a3-9de23f01c32f succeeded/healthy/validated.
Backed up and replaced the desktop helper; new worker is connected and sends
both pet and real Codex data. The user confirmed physical swipes and refresh.
The helper-off restart restored NVS history, an unanswered refresh kept values
and timestamp, and restarting the helper resumed fresh account updates while
staying on the usage page. Final Boot ID 16436990450825822716, same device and
firmware, normal/live/crash_count=0. The dedicated test client is closed and the
new desktop helper remains running. Hardware verification is complete for this
scope; evidence: docs/evidence/codex-usage/hardware-2026-11-07.md.
