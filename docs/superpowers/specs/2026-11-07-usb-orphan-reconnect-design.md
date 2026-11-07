# Recover inactive USB ownership during reconnect

The user approved adding the previously described recovery: confirm the former
session has exited and the port is free, clear its stale ownership through the
public Mosaico CLI, and reconnect without reflashing the board.

## Approach

Keep recovery in `tools/host_runtime.py`, alongside discovery and connection.
Use the selected USB control endpoint's structured ownership evidence from
`iris list --details`. Only an explicitly dead owner, an ordinary `owned`
claim, no transfer reservation, and a matching device identity qualify.
Call `iris reconcile --device-id ...`; its atomic session/physical-lock checks
remain the authority if ownership changes after discovery. Refusal stops this
connection attempt. Never kill a Gateway, edit its database, or use force.

Blind retries are the existing failure. Restarting or killing the Gateway
would interfere with shared clients. The approved public reconciliation path
handles this specific failure without either behavior.

## Scope and acceptance

- Support automatic and explicitly selected devices.
- Only one present Mosaico USB control endpoint may be automatically selected.
- Reject incomplete, conflicting, live, busy, or transferring ownership evidence.
- Preserve live firmware identity and pet RPC checks after reconciliation.
- Keep an automatically verified device selected across reconnection attempts.
- Add focused regression tests, rebuild the existing portable Windows EXE,
  replace the desktop copy after retaining a backup, and verify device sync.
- No firmware, partition, account-reader, UI, or upstream Gateway changes.

The implementation details and test structure are delegated to the implementer.

