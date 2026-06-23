# Bug Analysis Report — io-rts-esp32
**Date:** 2026-06-23  
**Method:** Four independent agents analysed the codebase in parallel across Security, Web UI, Firmware C++, and Protocol layers.

---

## Fix Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fixed and deployed |
| ⚠️ | Partially addressed / intentional design decision |
| ❌ | Not yet fixed |

---

## Executive Summary

| Layer | Critical | High | Medium | Low | Total |
|-------|----------|------|--------|-----|-------|
| Security | 2 | 10 | 5 | 3 | 20 |
| Web UI | 0 | 3 | 10 | 7 | 20 |
| Firmware C++ | 0 | 4 | 11 | 5 | 20 |
| Protocol | 0 | 2 | 8 | 5 | 15 |
| **Total** | **2** | **19** | **34** | **20** | **75** |

Five issues were independently flagged by two or more agents — those are marked ★ and carry extra confidence.

---

## CRITICAL

### C-1 — `GET /api/ota/key` returns auth key with no auth check ★ ⚠️
**Security · web_server.cpp:952–958**  
The OTA key that gates all privileged endpoints is returned to any anonymous LAN caller. Any attacker who calls this endpoint once gets the key needed to flash firmware, factory reset, or escalate to any other privileged endpoint. The design is self-defeating.  
**Fix:** Require `ota_check_key` on this endpoint; return the key only via the serial console.  
**Decision (2026-06-23):** Intentionally left open as a bootstrap mechanism — the browser must fetch the key before it can authenticate any other call. LAN is treated as trusted for now. `POST /api/ota/key` (key rotation) is protected.

### C-2 — `POST /api/ota/key` allows key rotation without auth ✅
**Security · web_server.cpp:962–994**  
Any LAN client can replace the current OTA key with an attacker-controlled value, then immediately use that key to authenticate all subsequent privileged operations.  
**Fix:** Add `ota_check_key` before processing the key rotation.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_ota_key_post`.

---

## HIGH

### H-1 — POST /api/action: unauthenticated physical device control ✅
**Security · web_server.cpp:464–593**  
Open, close, stop, set position, delete, deactivate, rename, calibrate, and pair operations on physical Somfy screens are all reachable with no auth check. Destructive operations (deleteDevice, calibrate, invertOpenClose) are irreversible.  
**Fix:** Add `ota_check_key` at the top of this handler.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_action_post`.

### H-2 — POST /api/mqtt: unauthenticated MQTT broker redirect ✅
**Security · web_server.cpp:661–714**  
An attacker can redirect the MQTT client to a malicious broker, enabling interception and injection of all device commands.  
**Fix:** Add `ota_check_key`.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_mqtt_post`.

### H-3 — GET /api/mqtt: MQTT password returned without auth ✅
**Security · web_server.cpp:641–657**  
`api_mqtt_get` returns the broker password in plaintext JSON to any anonymous LAN caller.  
**Fix:** Add `ota_check_key`, or at minimum redact the password from the unauthenticated response.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_mqtt_get`.

### H-4 — POST /api/wifi/config: network takeover without auth ✅
**Security · web_server.cpp:814–849**  
Reconfigures WiFi SSID/password and reboots — no auth. An attacker can take the device off the local network. Also uses a fixed 256-byte stack buffer for the body instead of the `read_body()` helper; a 256-byte Content-Length request silently truncates.  
**Fix:** Add `ota_check_key`.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_wifi_config_post`.

### H-5 — GET+POST /api/io/sniff: unauthenticated system key capture ✅
**Security · web_server.cpp:1522–1561**  
Enables the radio sniffer and retrieves captured io-homecontrol system keys. The system key is the master cryptographic secret for the entire Somfy radio network.  
**Fix:** Add `ota_check_key` to both handlers.  
**Fixed (2026-06-23):** `ota_check_key` added to both `api_io_sniff_get` and `api_io_sniff_post`.

### H-6 — POST /api/pair, /api/learn, /api/pair-device, /api/send-key: all unauthenticated ✅
**Security · web_server.cpp:2673, 2720, 2781, 2829**  
Four sensitive radio operations — initiate pairing, receive TaHoma system key, impersonate an actuator, relay key material — require no credentials.  
**Fix:** Add `ota_check_key` to all four POST handlers.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_pair_start_post`, `api_learn_start_post`, `api_learn_stop_post`, `api_pair_device_start_post`, `api_pair_device_stop_post`, `api_send_key_start_post`, `api_send_key_stop_post`, `api_capture_start_post`, `api_capture_cancel_post`.

### H-7 — WebSocket /ws: unauthenticated event stream ❌
**Security · web_server.cpp:184–242**  
Any LAN client can receive the live stream of device events, positions, and all ESP_LOG output — including the OTA key logged at boot (see H-8).  
**Fix:** Require auth token on WebSocket upgrade.  
**Deferred:** Browser WebSocket API does not support custom headers on upgrade requests. Requires a protocol-level workaround (e.g. token in URL query param or first message handshake).

### H-8 — OTA key logged in plaintext at boot → forwarded to unauthenticated WebSocket ★ ✅
**Security · web_server.cpp:775, 777**  
`ESP_LOGI(TAG, "OTA key (generated): %s", s_ota_key)` emits the full 32-char key on every boot. The `web_log_vprintf` hook forwards all log output to `/ws`. Combined with H-7, any client connected at startup receives the key automatically.  
**Fix:** Replace log with `"OTA key: set (32 chars)"` — never log the value.  
**Fixed (2026-06-23):** Log now emits `"OTA key (generated): set (%d chars)"` without the key value.

### H-9 — GET+POST /api/download+upload/devices: unauthenticated device DB access ✅
**Security · web_server.cpp:1886–1970**  
Full device registry (node IDs, names, paired remotes) can be exfiltrated or overwritten without credentials.  
**Fix:** Add `ota_check_key` to both endpoints.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_download_devices`, `api_upload_devices`, `api_download_remotes`, `api_upload_remotes`.

### H-10 — POST /api/remote/capture/start: unauthenticated remote injection ✅
**Security · web_server.cpp:2876–2899**  
Opens a 30-second window where any physical button press binds a remote to a device. An attacker on the LAN can open this window to inject unauthorized remote bindings.  
**Fix:** Add `ota_check_key`.  
**Fixed (2026-06-23):** `ota_check_key` added to `api_capture_start_post` and `api_capture_cancel_post`.

### H-11 — `sDeviceMap` written without `sMutex` in AddDevice/DeleteDevice/RestoreDevice ★ ❌
**Firmware C++ · IoHomeControl.cpp:795–841**  
`AddDevice`, `DeleteDevice`, and `RestoreDevice` insert into and modify `sDeviceMap` without holding `sMutex`. `UpdateDevicesStatusTask` simultaneously iterates the map. Concurrent insertions can corrupt the red-black tree causing memory corruption or undefined behaviour.  
**Fix:** Acquire `sMutex` at the start of each of these methods.

### H-12 — `UpdateDevicesStatusTask` reads `sDeviceMap` without `sMutex` ★ ❌
**Firmware C++ · IoHomeControl.cpp:730–787**  
The outer device loop reads `is_deleted`, timestamps, and `info.name` without any lock. `ProcessReceivedFrameTask` writes these fields under `sMutex`. Data race on all accessed `IoDevice` fields.  
**Fix:** Take `sMutex` before the outer loop, or snapshot the list under the mutex.

### H-13 — `ProcessReceivedFrameTask` holds `sMutex` during 500 ms blocking queue wait ★ ❌
**Firmware C++ · IoHomeControl.cpp:563–714 | Protocol · IoHomeControl.cpp:563–714**  
`sMutex` is acquired at line 563 and immediately passed to a 500 ms blocking `xQueueReceive`. All device-command operations (`SetDevicePosition`, HTTP handlers, status updates) are starved for up to 500 ms per cycle. In the passive key-sniff path, three sequential 500 ms waits under the mutex create a 1.5 s stall.  
**Fix:** Use non-blocking `xQueueReceive` inside the locked section, or release the mutex while waiting for frames.

### H-14 — IV construction bug in `create_key_transfer` silently corrupts all pairing as initiator ❌
**Protocol · io-homecontrol/protocol/iohome_commands.cpp:315**  
The IV for AES key encryption is built from only 1 byte (`[CMD_31]`), but every receiving path (passive sniff, `PairAsDevice`, `LearnKeyFromController`) uses a 7-byte IV: `[CMD_3C, 6-byte challenge]`. The resulting ciphertext is undecryptable at the receiver. Pairing falls through silently to `PAIRED_SHORTCUT_VERIFIED` masking the failure.  
**Fix:** In `create_key_transfer`, build the 7-byte IV as `[CMD_CHALLENGE_REQUEST, challenge[0..5]]`.

### H-15 — `DeviceGetBattery` moves physical devices ❌
**Protocol · IoHomeControl.cpp:2436–2446**  
Sends CMD_03 with `MainParam=0x0006` and `0x0009`. Per CLAUDE.md these are position commands that cause small actuator movement (~0% position). Battery data is reported autonomously via CMD_STATUS_UPDATE (0x71) and is not queryable this way.  
**Fix:** Remove or disable `DeviceGetBattery` until correct protocol parameters are determined.

---

## MEDIUM

### M-1 — API calls fire before OTA key is fetched (race condition) ✅
**Web UI · app.js:282–327**  
`loadMqttConfig`, `loadSyslogConfig`, `fetchAndDisplayDevices` fire concurrently with the async OTA key fetch. On first page load all three calls send requests without `X-OTA-Key`; if those endpoints require auth they silently fail.  
**Fix:** Await the key fetch before calling dependent config loaders.  
**Fixed (2026-06-23):** All three calls moved into `.finally()` block of the OTA key fetch in `app.js`.

### M-2 — `fetchFreshKey` rejection leaves OTA upload button permanently disabled ❌
**Web UI · ota.js:47, 266**  
Both `uploadFirmware` and `uploadWebUi` call `fetchFreshKey().then(...)` with no `.catch()`. If the key preflight fails, the button stays `disabled = true` forever with no error shown.  
**Fix:** Add `.catch(function(e) { finishWithError(app, "Key fetch failed: " + e.message); })`.

### M-3 — Null dereference on missing device-list element ❌
**Web UI · devices.js:403–404**  
`app.elements.deviceList.hasChildNodes()` is called with no null guard. If the element is absent, `TypeError` crashes the entire init sequence.  
**Fix:** Add `if (!list) return;` after the assignment.

### M-4 — POST /api/io/key and POST /api/ota/key sent without auth header ✅
**Web UI · ota.js:141–145, 219–223**  
Raw `fetch()` used instead of `window.MiOpenApi.postJson()` — `X-OTA-Key` never sent. Key editing from the UI silently returns 401.  
**Fix:** Replace with `window.MiOpenApi.postJson(...)`.  
**Fixed (2026-06-23):** Both calls in `ota.js` replaced with `window.MiOpenApi.postJson(...)`.

### M-5 — WiFi scan, reboot, file download bypass `otaHeaders()` ✅
**Web UI · settings.js:229, 508, 611; utils.js:127–136**  
Raw `fetch()` calls with no OTA key header. WiFi scan, both reboot paths, and `downloadFile` all silently 401 on protected endpoints.  
**Fix:** Use `window.MiOpenApi.requestJson/postJson` consistently; pass `{ headers: otaHeaders() }` in `downloadFile`.  
**Fixed (2026-06-23):** WiFi scan and both reboot calls in `settings.js` use MiOpenApi wrappers; `downloadFile` in `utils.js` now passes `otaHeaders()`.

### M-6 — TypeError from fetch silently treated as "Rebooting…" ❌
**Web UI · updater.js:127–130**  
Any `TypeError` in the update flow (including programming errors) displays "Rebooting…" and continues against an unresponsive device.  
**Fix:** Narrow the heuristic to only catch network-originated TypeErrors.

### M-7 — Sniff/learn/pairDevice timers not stopped on tab navigation ❌
**Web UI · settings.js:656–670**  
No `viewHidden` event is dispatched, so countdown and polling intervals keep running when the user navigates away from the Settings tab — indefinitely polling `/api/io/sniff` every 2 seconds.  
**Fix:** Dispatch `viewHidden` from the navigation handler, or cancel timers on any `viewShown` for a different view.

### M-8 — Double concurrent `fetchAndDisplayDevices` on page load ❌
**Web UI · app.js:151, 326–327**  
Called synchronously at startup and again immediately from the WebSocket `init` message. The two parallel fetches race to clear and rebuild the device DOM; the second overwrites `devicesCache` with potentially a stale snapshot.  
**Fix:** Debounce or serialise `fetchAndDisplayDevices`.

### M-9 — `ConfigureRadio`: SetBitRate / SetFrequencyDeviation / SetOutputPower return values discarded ★ ❌
**Firmware C++ · IoHomeControl.cpp:470, 486, 494 | Protocol · iohome_commands.cpp:470–499**  
Each call's return value is not stored in `state`; the guard below checks the previous call's result. Radio misconfiguration on any SPI error goes undetected.  
**Fix:** `state = mRadio->SetBitRate(BIT_RATE);` for each of the three calls.

### M-10 — `sSniffedKey` 33-byte array has data race ❌
**Firmware C++ · IoHomeControl.cpp:88, 614, 2477, 2496**  
Written under `sMutex` in `ProcessReceivedFrameTask`; read in `GetSniffedKey()` and cleared in `StartKeySniff()` without any lock. A torn read can produce a corrupted key string.  
**Fix:** Protect `sSniffedKey` with `sMutex` in both `GetSniffedKey` and `StartKeySniff`.

### M-11 — TOCTOU: `sDeviceMap.find()` + `is_deleted` read before taking `sMutex` ❌
**Firmware C++ · IoHomeControl.cpp:1454, 1510, 1575, 1596**  
`SetDevicePosition`, `SetDeviceTilt`, and sibling functions read the map and `is_deleted` without the mutex, then take the mutex later. State read before the lock is stale by the time the lock is held.  
**Fix:** Take `sMutex` before `sDeviceMap.find()`.

### M-12 — `xTaskCreate` return value unchecked; heap leaks on failure ❌
**Firmware C++ · web_server.cpp:574, 2681, 2734, 2789, 2837**  
Five session task creation sites (`calibration_task`, `pairing_task`, `learn_task`, `pair_device_task`, `send_key_task`) do not check the `xTaskCreate` return. On failure the `arg` heap allocation leaks, and the session state variable (`s_cal_device_id` etc.) remains set, blocking future operations until reboot.  
**Fix:** Check return value; free `arg` and clear state on failure.

### M-13 — `WaitAndRespondToCmd28` accepts CMD_28 addressed to any node ❌
**Protocol · IoHomeControl.cpp:1169–1176**  
CMD_28 frames addressed to other controllers on the same frequency trigger a CMD_29 response from this device, injecting spurious devices into other hubs' pairing scans.  
**Fix:** Accept only broadcasts (`BROADCAST_DISCOVER_ADDRESS`) or frames addressed to `mOwnNodeId`, matching `PairAsDevice`'s pattern.

### M-14 — `DiscoverAndPairDevice` accepts CMD_29 without checking dest_node ❌
**Protocol · IoHomeControl.cpp:862–865**  
A CMD_29 from a concurrent pairing session (destined for another controller) is accepted, causing the device to proceed with 2C/31/32 toward a foreign device, corrupting its pairing state.  
**Fix:** Add `memcmp(rxItem.frame.dest_node, mOwnNodeId, NODE_ID_SIZE) == 0` as an additional condition.

### M-15 — `PairAsDevice` waits to receive CMD_3C instead of sending it ❌
**Protocol · IoHomeControl.cpp:1394–1410**  
After receiving CMD_31 from TaHoma, the code blocks waiting to receive CMD_3C. But in the protocol the device (not the controller) is supposed to send CMD_3C as a challenge. TaHoma stalls waiting for that challenge and never sends CMD_32. Pairing as a device never succeeds.  
**Fix:** After receiving CMD_31, emit CMD_3C, then wait for CMD_3D, then wait for CMD_32, then send CMD_33.

### M-16 — `SendAndReceive` silently drops unrelated frames from the queue ❌
**Protocol · IoHomeControl.cpp:1993–1999**  
Frames from concurrent devices (e.g. spontaneous CMD_STATUS_UPDATE) arriving during a `SetDevicePosition` exchange are consumed from `sRxIoQueue` and discarded. With 2+ active devices, all 3 retries can be exhausted by unrelated frames.  
**Fix:** Push unrecognized frames back with `xQueueSendToFront`, or only skip broadcast-type commands.

### M-17 — `LearnKeyFromController` and `WaitAndRespondToCmd28` hold `sMutex` across 2 s poll cycles ★ ❌
**Protocol + Firmware C++ · IoHomeControl.cpp:1036–1066, 1157–1182**  
Both functions hold `sMutex` for the full 2-second polling window per retry. A 120-second learn session effectively locks out all device commands from the web UI for its entire duration.  
**Fix:** Release the mutex before blocking `xQueueReceive`; re-take it only for the transmit/process step.

### M-18 — Tick constants computed with inverted formula ❌
**Firmware C++ · IoHomeControl.cpp:32–34, 268+**  
`N * portTICK_PERIOD_MS` multiplies instead of divides. Result is correct only at 1000 Hz where `portTICK_PERIOD_MS == 1`. At any other tick rate all timeouts are wrong by orders of magnitude.  
**Fix:** Replace all occurrences with `pdMS_TO_TICKS(N)`.

### M-19 — `RadioSX1276::Init()` not idempotent — re-call leaks queue, spawns duplicate task, double-registers ISR ❌
**Firmware C++ · RadioSX1276.cpp:199–224**  
Re-calling `Init()` creates a new GPIO queue (leaking the old handle), spawns a second `gpio_task`, and re-registers ISR handlers, causing two events per GPIO edge.  
**Fix:** Guard resource creation with a first-init flag; tear down old resources before re-init.

### M-20 — `Calibrate()` busy-waits on IMAGECAL_RUNNING with no timeout ❌
**Firmware C++ · RadioSX1276.cpp:737–739, 756–758**  
If the SX1276 is damaged or SPI is broken the flag never clears, spinning the task forever and eventually triggering the task watchdog.  
**Fix:** Add a deadline counter and return `RADIO_ERR_HARDWARE` on timeout.

### M-21 — `mReceiving`, `mPassiveMode`, `mInitialized` are plain `bool` without synchronization ❌
**Firmware C++ · IoHomeControl.hpp:281–284**  
Written and read across multiple tasks without `volatile` or `std::atomic`. Compiler can cache reads in registers or reorder writes.  
**Fix:** Declare as `std::atomic<bool>` or at minimum `volatile bool`.

### M-22 — Uninitialized `item3c.frame.command_id` logged after queue timeout ❌
**Firmware C++ · IoHomeControl.cpp:1216–1224**  
When `xQueueReceive` returns `false` (timeout), `item3c` is uninitialized. The log statement inside the block dereferences `item3c.frame.command_id` — undefined behaviour.  
**Fix:** Zero-initialize: `RxFrameQueueItem item3c = {};`.

### M-23 — `LinkRemoteToDevice` reads `sDeviceMap` without `sMutex` ❌
**Firmware C++ · IoHomeControl.cpp:1748**  
Called from the HTTP handler task; reads the device map while only `sRemoteMapMutex` is held, creating a data race with `ProcessReceivedFrameTask`.  
**Fix:** Acquire `sMutex` before `sDeviceMap.find()`.

### M-24 — `DeviceGetGenericInfo` missing task priority elevation before `SendAndReceive` ❌
**Protocol · IoHomeControl.cpp:2392**  
All other callers of `SendAndReceive` elevate task priority first. Low-power devices have a short RX window; at default priority a context switch can cause the response to arrive in the FIFO before the app-layer timer expires.  
**Fix:** Add `vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK)` / restore around the call.

### M-25 — GET /api/somfy/credentials, /api/wifi/fallback, /api/io/config unauthenticated ⚠️
**Security · web_server.cpp (various)**  
Somfy account email, fallback AP config, controller node ID / TX power, and remote-device link tables are all readable or writable without credentials.  
**Fix:** Add `ota_check_key` to all five endpoints.  
**Decision (2026-06-23):** `api_download_remotes` / `api_upload_remotes` protected (covered under H-9). `api_somfy_credentials_get`, `api_wifi_fallback_get`, and `api_io_config_get` intentionally left open — these three are called by the UI at startup before the OTA key is available in the browser, and they contain non-sensitive data on a trusted LAN.

---

## LOW

### L-1 — OTA key compared with `memcmp` — timing side-channel ★ ✅
**Security · web_server.cpp:788 | Firmware C++ · web_server.cpp:788**  
`memcmp` exits on first mismatch, leaking key prefix length via response time. Flagged by both agents.  
**Fix:** Use `esp_crypto_memcmp` or manual XOR-accumulate for constant-time comparison. (A `constant_time_compare` function already exists in `MiscConfig.cpp`.)  
**Fixed (2026-06-23):** Replaced with XOR-accumulate constant-time comparison in `ota_check_key`.

### L-2 — Default io-homecontrol system key is a well-known placeholder ❌
**Security · sdkconfig:789**  
`CONFIG_IOHOMECONTROL_DEFAULT_KEY = "00112233445566778899AABBCCDDEEFF"`. A freshly flashed device that has never been provisioned uses this key. Any attacker who knows the default can join the radio network.  
**Fix:** Refuse to initialize the radio if the key equals the placeholder; require explicit provisioning.

### L-3 — `web_server_broadcast_log` JSON escapes only double-quotes ❌
**Security · web_server.cpp:290–300**  
Backslash and control characters (`\n`, `\r`, `\t`) in log lines produce malformed JSON, breaking WebSocket clients.  
**Fix:** Implement full RFC 8259 string escaping.

### L-4 — ISR passes NULL `pxHigherPriorityTaskWoken` to `xQueueSendFromISR` ❌
**Firmware C++ · RadioSX1276.cpp:29–30**  
Frame reception events are not processed until the next tick, adding up to 1 ms latency to preamble detection.  
**Fix:** Use a local `BaseType_t woken = pdFALSE;` and call `portYIELD_FROM_ISR(woken)`.

### L-5 — `ws_send_str`: `malloc` failure leaves dead fd in `s_ws_fds[]` ❌
**Firmware C++ · web_server.cpp:159–167**  
On allocation failure the fd is never removed; future broadcasts keep attempting and failing to send to it.  
**Fix:** Remove the fd from `s_ws_fds[]` before returning on `malloc` failure.

### L-6 — `SpiReadByte` returns `0xFF` on SPI error; callers cannot distinguish failure ❌
**Firmware C++ · RadioSX1276.cpp:700–705**  
`0xFF` is also a valid register value. SPI failures can spuriously trigger payload-ready and sync-word handlers.  
**Fix:** Propagate the error code, or use an out-parameter and have callers check for failure.

### L-7 — `create_challenge_request` missing START flag in `init_frame` ❌
**Protocol · iohome_commands.cpp:330**  
CMD_3C sent without START flag in CTRL0. Some devices may require it to recognise the frame as the beginning of a new sub-exchange.  
**Fix:** Change to `init_frame(frame, true, true, false, false)`.

### L-8 — CMD_2C sent with LONG_PREAMBLE (~213 ms) to an already-awake device ❌
**Protocol · iohome_commands.cpp:238–245**  
`init_frame` with `is_start=true` triggers LONG_PREAMBLE in `SendAndReceive`. CMD_2C goes to a device that just replied with CMD_29; a 213 ms preamble risks the device's response window expiring before CMD_2C finishes.  
**Fix:** Set `is_start=false` in CMD_2C's `init_frame` call to use SHORT_PREAMBLE.

### L-9 — `devicesFileInput` / `remotesFileInput` never assigned — dead stubs ❌
**Web UI · settings.js:678, 688**  
`app.uploadDevices` and `app.uploadRemotes` reference `app.elements.devicesFileInput` / `remotesFileInput` which are never set. Calling either crashes immediately with TypeError.  
**Fix:** Either complete the feature (add the file input elements) or remove the stub functions.

### L-10 — WebSocket parse errors silently swallowed ❌
**Web UI · app.js:204**  
The entire WS message handler is inside a `try/catch` with `/* ignore parse errors */`. Downstream handler exceptions (not just JSON.parse failures) are silently discarded.  
**Fix:** Only wrap `JSON.parse` in try/catch; let handler exceptions propagate or log them.

### L-11 — Major-upgrade banner version comparison produces NaN ❌
**Web UI · updater.js:184**  
`parseInt(currentVersion.slice(1))` produces `NaN` if the version string has no `v` prefix, so the major-upgrade banner never appears.  
**Fix:** Normalise with `.replace(/^v/, "")` before `parseInt`, same as `isNewer()` already does.

### L-12 — Various other low-severity Web UI issues ❌
- `r.ok` dead code in `saveFallbackConfig` (`settings.js:77`) — always `undefined`
- `devicesCache.forEach` without null guard in `popup.js:52`
- Misleading error when remote-table tbody is absent (`remotes.js:21`)
- `updateFavButton` tooltip not localised (`devices.js:163`)
- Double `getElementById` in `uploadWebUi` (`ota.js:251`)

---

## Additional fixes not in original report

### X-1 — Trailing spaces in syslog/MQTT server addresses cause DNS resolution failure ✅
**Web UI · settings.js; Firmware C++ · web_server.cpp**  
A space accidentally typed after a hostname (e.g. `192.168.178.71 `) was stored verbatim in NVS and passed to the DNS resolver, which failed to resolve it.  
**Fixed (2026-06-23):** `.trim()` applied to `server`, `user`, `client_id`, `topic`, `discovery` fields in `updateMqttConfig` and `updateSyslogConfig` in `settings.js`. Server-side `trim_str()` helper added to `web_server.cpp` and applied in both `api_mqtt_post` and `api_syslog_post`.

### X-2 — Default syslog server pre-filled ✅
**Firmware C++ · main/Kconfig.projbuild**  
Fresh devices had no syslog server configured, requiring manual entry.  
**Fixed (2026-06-23):** Default server set to `syslog.speijkers.nl`, port `5144` in Kconfig. Syslog remains **disabled** by default — the user must explicitly enable it in the UI.

---

## Cross-Cutting Themes

**1. Mutex coverage is inconsistent.**  
`sMutex` protects the radio exchange path well but is not consistently applied to `sDeviceMap` writes (H-11) or reads (H-12, M-11, M-23). AddDevice / DeleteDevice / UpdateDevicesStatusTask all race against each other.

**2. Auth is applied endpoint-by-endpoint but many endpoints were never covered.**  
The pattern of `if (!ota_check_key(req)) { ... return; }` exists and works, but was only applied to a subset of endpoints. The auth model is correct; coverage is not. Nearly every POST handler and several GET handlers is missing the check. **The auth sweep in this session covered all remaining POST endpoints and sensitive GET endpoints.**

**3. Raw `fetch()` used instead of `window.MiOpenApi.requestJson/postJson` in several Web UI paths.**  
`otaHeaders()` is only injected by the `MiOpenApi` wrappers. Direct `fetch()` calls bypass it silently and fail with 401 on auth-protected endpoints. **Fixed for WiFi scan, reboot, file download, and key rotation paths.**

**4. Blocking operations under `sMutex`.**  
Multiple code paths hold `sMutex` for up to 2 seconds while blocking on `xQueueReceive`. This starves all device-command operations from the HTTP handler tasks during pairing, learning, and normal frame processing.

**5. Protocol role confusion in `PairAsDevice`.**  
The device-role pairing sequence (M-15) has the controller and device roles reversed in the CMD_3C step, meaning "Pair as Device" has never worked correctly.

---

## Recommended Priority Order

1. ~~**C-1, C-2**~~ — C-2 fixed; C-1 intentionally open (bootstrap).
2. ~~**H-1, H-5, H-6, H-8, H-9, H-10**~~ — Auth sweep complete. **H-7** (WebSocket) deferred.
3. **H-11, H-12, H-13** — Mutex correctness: `sDeviceMap` races and mutex-under-queue-wait.
4. **H-14** — IV construction in `create_key_transfer` (silent pairing failure).
5. **H-15** — `DeviceGetBattery` physically moves screens.
6. **M-15** — `PairAsDevice` role reversal (feature has never worked).
7. ~~**M-1, M-4, M-5**~~ — Web UI auth header gaps: all fixed.
