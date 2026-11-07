# Codex usage integration verification

Verified on Windows on 2026-11-07, branch `feat/codex-usage-screen`, based on
`0e2e78ab2c1fcdc1d36ce7cf6fc8ef161ff3e094`.

This records the integration stage. The subsequent parallel-task fix and final
Windows helper are recorded in [parallel activity verification](../parallel-activity-2026-11-07.md).

## Automated checks

- `python -m unittest discover -s tools/tests -q`: **29 passed**.
- `python -m pytest tools/usage_agent/tests -q`: **35 passed**.
- Native C `ctest --test-dir build-native -C Release --output-on-failure`:
  **4 passed** (payload, NVS retention, controller lifecycle, scene routing).
- Targeted Ruff checks passed; `git diff --check` passed.

The worker tests cover immediate cached startup, coalesced slow queries, failure
retention, legacy migration, failed cache writes, cancellation, refresh requests,
legacy firmware, shared transport and reboot without an observed USB disconnect.
Native tests cover hidden-page updates, timeout, payload merging and retained NVS.
No model calls or real account data are used by these automated tests.

## Shared native simulator

The actual GSP backend was built and exercised through simulator drags and taps,
including page swipes, header navigation, Wi-Fi and volume settings, manual
refresh, repeated-tap suppression, failed refresh, the actual 30-second timeout,
recovery, and pet updates while its page was hidden.

Local action logs:

- Normal: `.codex-runs/two-screens-4d17f242/actions.json`.
- Cached/offline: `.codex-runs/two-screens-ca8dd9cf/actions.json`.
- First run without saved data: `.codex-runs/two-screens-5140f626/actions.json`.

Screenshots contain **fixture values, not the user's account statistics**:

| Case | Screenshot |
| --- | --- |
| Pet home | [pet-home.png](pet-home.png) |
| Remaining-quota ring | [usage-main.png](usage-main.png) |
| Two quota windows | [usage-dual.png](usage-dual.png) |
| Failed read retains values | [usage-failed-retained.png](usage-failed-retained.png) |
| Timeout retains values | [usage-timeout-retained.png](usage-timeout-retained.png) |
| Successful recovery | [usage-recovered.png](usage-recovered.png) |
| Cached startup | [usage-offline.png](usage-offline.png) |
| No previous data | [usage-empty.png](usage-empty.png) |
| Wi-Fi settings | [wifi-settings.png](wifi-settings.png) |
| Volume settings | [volume-settings.png](volume-settings.png) |

## Firmware and Windows helper

SDK checkout verified at `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`, target
`esp32s31`. Build doctor and firmware build passed. Final source build log:
`.codex-runs/idf-low-noise-build/20261107-150936-build-52856/raw.log`.
The existing documented BSP display and Windows Gateway patches were applied
to the workspace dependencies; no managed components or partition layout changed.

The standalone [Windows helper](../../../tools/windows/README.md)
contains Python 3.14.10, Iris dependencies and all migrated usage modules.
Its isolated self-test passed with `usage_modules=true`, `usage_protocol=1`
and successful USB discovery. The final packaged Python also completed a real,
read-only Codex account query for remaining quota and lifetime tokens.
Neither check claimed the board, wrote firmware or changed the account.

Integration-stage EXE SHA-256 (superseded by the parallel-task release):
`0cfc6fce305316b4d66f3f770a6dfee672883f062b13f06590d8e9bd9653f01d`.
The original local EXE is preserved in
`.codex-runs/windows-build/MosaicoAgentPet-before-integration.exe`.

The complete local firmware bundle, `build/Agent_Pet-system-update.irisfw`,
was built and inspected successfully (3,842,011 bytes). It contains the unchanged
partition table, application (2,460,128 bytes), and both scenes' UI resource image
(4,200,784 bytes). It contains no NVS image. Build and inspection evidence:
`.codex-runs/system-update-bundle-final.log` and
`.codex-runs/system-update-inspect.json`.

Bundle SHA-256:
`f291153e3d5ed62f9f492c5753c18f2cbfdd80c0b21bd98c30d5927c207b84c1`.
The packaging target uses the workspace's normal Iris Python environment;
the helper's isolated embedded runtime is for running the helper, not building
firmware bundles.

## Review and completed hardware acceptance

Independent review found one material issue: a quick reboot could leave the
usage refresh counter at zero and skip resend. Pet replies now include the
lossless Iris boot ID; a change invalidates delivery state and requests fresh
collection. The regression failed before the fix and passes in the full suite.

The user subsequently requested hardware testing. The complete bundle was
installed successfully and the desktop helper was replaced with the verified
new EXE. Physical page navigation/refresh, helper-off reboot restoration, timeout
retention and resumed background synchronization passed. See
[hardware verification](hardware-2026-11-07.md) for live identity, operation
evidence and the exact tested scope. Installing only the
app binary is insufficient because this change also updates the UI resource image.
