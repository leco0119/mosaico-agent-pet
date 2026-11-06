# Agent Pet voice

`app_voice` owns the physical AI key, XiaoZhi cloud session,
and half-duplex Opus audio on the Mosaico BSP. The worker opens the microphone
only during a manual turn. After release, it drains the reply and restores
the pet through the UI task. The USB link continues receiving status during a turn.

The worker follows the link generation published by `app_wifi` and opens a
new session after a network change. `esp_xiaozhi` retrieves the official cloud
configuration and negotiates the transport. Its client UUID and transport
settings live in the application's NVS. Enrollment uses the board's MAC and
activation v1 on both the initial info and activation requests.
Firmware updates remain on the Mosaico System Update path.

Call `app_voice_start(&voice, wifi)` once after NVS and Wi-Fi initialization. The returned
handle belongs to the application until reboot. Failure leaves the handle
NULL and the pet can continue in USB mode. Call `app_voice_read(voice, &snapshot)`
from any task. It copies a bounded snapshot and performs no network or audio
work. `revision` changes when the displayed state changes. `completed_turns`
counts successful replies with audio since boot. The six-digit `binding_code`
is available to the UI while enrollment is pending. The volume snapshot uses
0–100 percent. The UI can apply a live change, then request NVS persistence
when the user releases the slider. Preview plays a local tone on the same codec.

The voice worker owns all codec and session calls. Its stack stays in internal
RAM because both volume and cloud configuration can write to NVS. Button,
event and transport callbacks set event flags or enqueue an Opus packet in
the bounded PSRAM reply buffer. The UI task owns the GSP screen. Capture uses
16 kHz mono, and reply playback uses 24 kHz mono. Holding
the key is limited to 30 seconds per turn. The UI returns to USB status after
reply completion or a brief error notice.

Board audio settings and shared Wi‑Fi integration were checked against the
[Mosaico XiaoZhi reference](https://github.com/lisir233/esp-mosaico-xiaozhi/tree/b62ee6a/main/boards/espressif/esp-mosaico).
