# Agent Pet Wi-Fi

Start `app_wifi_start(&wifi)` once after NVS initialization. The service owns the
station until reboot. The UI submits scans and connects without blocking on the
radio, and reads `app_wifi_read()` on its render task. Cloud clients read
`app_wifi_link()` and drop a session if its connection generation changes.

The worker verifies the associated SSID and an IP address before publishing
connectivity. It keeps four successful networks in the application's NVS and
uses the Vibe Mode credentials through the read-only shared ABI. A failed manual
switch leaves the profiles intact and retries the previous working network.
