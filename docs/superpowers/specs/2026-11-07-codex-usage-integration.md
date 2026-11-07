# Agent Pet + Codex usage

Approved in chat on 2026-11-07: integrate usage_panel into Agent Pet, with the
existing pet on screen 1 and the approved Codex ring dashboard on screen 2.

- One Agent_Pet firmware and one MosaicoAgentPet.exe. Horizontal swipes switch
  the two main screens; boot opens the pet. Background updates never change pages.
- Existing pet providers, Wi-Fi, volume and Xiaozhi voice remain available.
  Settings disable page swipes until returning home. The usage page is a main
  screen, not a settings modal; voice is still available there.
- The second screen displays remaining quota, reset time, lifetime tokens,
  update time and refresh action. Only Codex account usage is collected.
- PC collection uses the existing authenticated Codex app-server read methods.
  It runs independently of pet heartbeats, without opening a second USB client.
- Initial connection pushes cached data immediately. Startup, reconnect,
  manual refresh and a 5-minute background interval trigger collection.
  Concurrent requests coalesce. Account/network failure never disconnects the pet.
- Computer cache survives EXE upgrades; migrate a valid legacy mosaico-usage
  cache only if the new cache is absent. Device NVS retains the existing
  usage_panel/snapshot_v1 record and restores it as historical data on boot.
- Keep the independent usage_panel source intact. Package migrated modules
  inside agent_pet, with no runtime dependency on the sibling repository.
- Use ESP-IDF 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe, GSP 1.5.1,
  compiler 0.6.1, 480x480 RGB565 and the existing partition layout.

Acceptance: native simulator swipes and refresh, settings isolation, pet
updates while usage is visible, error/timeout retention; PC worker and
transport regression tests; pinned firmware build and packaged EXE self-test.
Hardware acceptance is separate and requires live device identity and evidence.
