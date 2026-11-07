# USB Reconnect Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically recover a dead USB owner's ordinary claim before reconnecting the same Mosaico.

**Architecture:** Extend the existing host transport with validated discovery evidence and public CLI reconciliation. ESP-Iris retains responsibility for atomic liveness, physical-port locks, and transfer protections. Keep the pet runner and device firmware unchanged.

**Tech Stack:** Python 3.14 standard library, unittest, existing Windows C# portable launcher.

**Spec:** `docs/superpowers/specs/2026-11-07-usb-orphan-reconnect-design.md`

## Global Constraints

- All device operations use `mosaico.py`.
- Never kill a Gateway, edit ownership storage, force a takeover, or write firmware.
- Reconciliation requires explicit `owner_alive=false`, `state=owned`, no transfer, and a matching selected device.
- Reuse the pinned portable Python and Iris dependencies.

## Review Focus

- Ownership or physical locks change after discovery: public reconciliation must refuse and block the claim.
- Missing/malformed ownership fields: fail closed without reconciliation.
- Multiple boards or a different board after unplugging: never silently recover or connect the wrong device.
- Explicit selection and automatic selection both receive recovery.
- Packaging must include the modified transport and preserve the shared usage cache.

### Task 1: Recover stale claims and deliver the helper

**Files:** Modify `tools/host_runtime.py` and `tools/windows/README.md`; create `tools/tests/test_usb_reconnect.py`.

**Interfaces:** Consume existing discovery `endpoints[].ownership` and `iris reconcile --device-id`; preserve `present()`, `connect_auto()`, and `start_lease()` for the runner. Do not change firmware RPCs.

- [ ] Write tests using a stateful CLI substitute at the subprocess boundary. Demonstrate that a dead ordinary claim prevents auto/explicit connection until reconciled; assert live/unknown owners, reservations, identity mismatches, multiple devices and refused reconciliation cannot be cleared or connected.
- [ ] Run `python -m unittest discover -s tools/tests -p test_usb_reconnect.py -v`; expect the recovery cases to fail before implementation.
- [ ] Implement the minimal discovery validation and reconciliation in the transport. Retain successful device identity for reconnect selection and log successful recovery.
- [ ] Run the focused tests, then `python -m unittest discover -s tools/tests -v` and the usage collector tests; expect all tests to pass.
- [ ] Document automatic recovery conditions, review the complete diff, and commit the fix on `fix/usb-orphan-reconnect`.
- [ ] Obtain a fresh review of the complete change; address material findings with regression coverage.
- [ ] Rebuild the portable EXE with the already verified Python ZIP and matching Iris environment; run its self-test. Expected: exit 0 and `ok=true`.
- [ ] Gracefully close only the old helper, retain a backup, replace its desktop EXE and start it. Use public CLI handoff if the old helper Gateway still owns the device; do not terminate that Gateway.
- [ ] Verify live same-device pet RPC and usage delivery, and ask the user for a real unplug/replug check. Record results and any remaining gaps under `.codex-runs/usb-orphan-fix`.

