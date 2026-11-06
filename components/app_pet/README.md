# app_pet

Owns the single Agent Pet screen and its A/P/D/T status protocol. The same
`app_pet.c` controller runs on ESP32-S31 and in the native GSP 1.5.1 simulator.
The scene contains four root-level compiled animations and three labels; there
is no PageFlow, counter, OA list, motor or LED behavior.

`main` creates the controller, then calls `status_link_start(pet, ui)`. The
controller borrows the UI and owns its heartbeat timer. On device both live until
reboot. On Host, stop dispatch before destroying the controller and closing GSP.
No API is ISR-safe. Create clears its output on failure; destroy keeps ownership
if stopping its timer fails.

After startup the GSP UI task is the controller's single owner. Iris callbacks
copy bounded updates into a mutex-protected, latest-value mailbox. A 50 ms GSP
timer drains it and publishes a value-copy diagnostic snapshot. Iris workers
never mutate the UI or controller. The transport is an application-lifetime
singleton; it rolls back failed registration and timer/mutex initialization.

RPC service `0x5354`, method `0x0001`, accepts UTF-8 lines:

```text
T=Codex
A=1
P=运行命令
D=exec_command
```

`A` is 0 idle, 1 working, 2 waiting, 3 done. A payload is at most 512 bytes;
individual lines are shorter than 128 bytes including `key=`. Unknown or invalid
fields are ignored. SET acknowledges queueing; a newer pending status can replace
an older one. Method `0x0002` returns the applied animation, host-connected flag,
update count and last GSP error. It no longer exposes the old module test inputs.

Recognized updates renew a 30-second heartbeat. On expiry the UI returns to the
idle pet with `等待连接 / USB`. Animation rendering remains owned by GSP; no
application task manually advances frames. A controller snapshot is read on the
UI task, or after dispatch has stopped; transport clients read its published copy.

Native preview normally shows the waiting screen. For a timed state demonstration,
set `MOSAICO_PET_SIM_CYCLE=1` before `mosaico.py project sim`; the Host adapter then
feeds all four states through the same portable parser every two seconds.
