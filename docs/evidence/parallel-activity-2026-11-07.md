# Parallel pet activity verification — 2026-11-07

The Windows helper now combines concurrent sessions instead of selecting only
the most recently written log. A completed turn cannot hide another working
turn. Working tasks take precedence over waiting tasks, followed by completed
or idle tasks; recency selects a task within each group.

## Sources and behavior

- Local WorkBuddy, Claude Code, Codex and PandaCode logs are classified separately.
  Parsed event tails are cached until file size or modification time changes.
- `tools/desktop_activity.py` reads live Codex and ChatGPT status from the desktop
  app's local app-tools pipe, calling only `list_threads`. The observed desktop
  version was `26.1103.4239.0`.
- A live active Codex task stays busy even during a long quiet period or when its
  log tail contains only metadata. A live idle result corrects an incomplete log.
- ChatGPT history cache was rejected as a live source: during the user's test,
  the app reported the ChatGPT task active while the SQLite catalog still held
  the previous idle result. SQLite is used only to locate an existing local chat
  ID for attribution when the helper starts outside a Codex shell.
- The adapter rediscovers the pipe on desktop restart, polls at most once per
  three seconds, and bounds a read cycle to three seconds. Short read failures
  retain state for at most 15 seconds. Closing the app clears its live source.
- Pinned chats are included. When the recent list reaches 50 entries, older
  ChatGPT history is scanned incrementally. Fresh idle results replace older
  active records. No messages are sent, and no authentication material or chat
  message content is read or stored by the adapter.

## Automated checks

Commands were run from the project directory:

```text
python -m unittest discover -s tools/tests -p 'test_*.py'
44 tests passed

python -m pytest tools/usage_agent/tests -q
35 tests passed

python -m ruff check tools/agent_pet.py tools/desktop_activity.py \
  tools/tests/test_parallel_activity.py tools/tests/test_desktop_activity.py \
  tools/tests/test_agent_pet.py tools/windows/build.py \
  tools/windows/portable_agent.py tools/tests/test_windows_package.py
All checks passed
```

Regression cases were first observed failing, then passing after the fixes:

- one turn completes while another local turn remains active;
- a newer completed source masks a running Codex session;
- Codex finishes while ChatGPT remains active, then ChatGPT finishes;
- active tasks with old or metadata-only log tails;
- deleted logs and malformed tool names;
- short desktop read failures, expiration, and desktop exit;
- older ChatGPT history pages and stale active rows following a fresh idle result.

The final EXE's independent self-test passed with Python 3.14.10, both usage and
desktop activity modules imported, and USB discovery successful. Installed
`agent_pet.py` and `desktop_activity.py` hashes matched the checked source files.

## Installed artifact

- Desktop: `%USERPROFILE%/Desktop/MosaicoAgentPet.exe`
- Local distribution copy: `tools/windows/MosaicoAgentPet.exe` (excluded from Git).
- SHA-256: `16a1a1795a6b2267b7fa6505288ad427987d3d4bc21b59bb4987e88596d755ea`
- Payload: `%LOCALAPPDATA%/MosaicoAgentPet/payload-5a67ea0c0bf4f809`
- Pre-fix backup: `.codex-runs/windows-build/MosaicoAgentPet-before-parallel.exe`

The helper was launched without inherited `CODEX_THREAD_ID` or
`CODEX_APP_TOOLS_PIPE_PATH`, confirming ordinary desktop startup works. Each
previous helper was stopped normally; its idle device claim was released with
the public CLI. No Gateway was killed. The final helper was left running.

## Live verification

The installed adapter simultaneously reported a real Codex task and a separate
ChatGPT task active.

A separate read-only diagnostic ran the production selector using those live
states with the current Codex chat omitted. It selected `ChatGPT / think`, not
idle. This diagnostic changed only its own in-memory input; it did not finish a
chat, modify a session log, or inject a test status onto the board. Actual turn
completion was subsequently checked on hardware as recorded below.

The same board stayed on Agent_Pet 1.2.3 without flashing or rebooting:

- Firmware SHA-256: `d0923c646e20c1770a9da291a1065864f634ecaa5baba699d81d270adb80e89a`.
- Live normal firmware, `stale=false`, crash count `0`.
- Pet status: `connected=true`, working animation `1`, update count `190`,
  `last_error=0`.
- The captured first screen displayed Codex working with the existing pet UI.

Local raw evidence is under `.codex-runs/parallel-activity/`:
`final-helper.json`, `final-device.json`, `final-pet.json`,
`final-desktop-source.json`, and `final-screen.png`. The separate source probe
reported `available=true` with no error. The isolated self-test report was saved
locally as `portable-smoke.json`.

## Verification after the Codex turn actually ended

The Codex turn completed at **2026-11-07 16:16:36 UTC+08**. A separate read-only
checker then observed the desktop app, installed helper and physical board for
**60.88 seconds**, taking **10 samples** between 16:16:42 and 16:17:43.

Every sample reported Codex idle, ChatGPT active, the helper selecting
`ChatGPT / think`, and the board showing working animation `1`. The board's
update counter continued advancing. No task state, session log or board status
was injected or modified by this checker. The result was **PASS**.

![Physical board after Codex completed while ChatGPT continued](parallel-activity-after-final.png)

The bounded checker completed normally. Raw local records are under
`.codex-runs/after-turn-01a1b510/` and are excluded from the source release.

ChatGPT activity requires the desktop app to remain running (minimized is fine).
The app-tools interface is internal and version dependent. This requirement is
separate from Codex quota collection, which continues to use the official
app-server account reader and does not require the desktop window to be open.
