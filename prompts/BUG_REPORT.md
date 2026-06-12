# io-rts-esp32 — Bug & Security Analysis Report

*Original: 2026-06-09. Updated: 2026-06-12 via 4-angle review (C++ backend, JavaScript/Web UI, Security/Auth, ESP-IDF/Embedded).*

---

## Already Fixed

The following bugs from the original report were resolved in commits `7676168` and `6eece19`:

| ID | Fix |
|---|---|
| H1 | MQTT bounds `\|\|` → `&&` |
| H2 | `mIoPassive` initialised from `IoHomeConfig` at boot |
| H3 | `esp_timer_delete` in `confirmation_poll_cb` |
| H4 | Loop on `httpd_req_recv` in `read_multipart_content` |
| H5 | Magic-byte check on URL-OTA firmware path |
| H6 | Reject WebSocket clients when all slots full |
| H9 | `status=restarting` treated as success in `saveWifiConfig` |
| H10 | `ok=true` treated as success in `saveFallbackConfig` |
| H11 | Error toast shown on `loadNetworkConfig`/`loadIoConfig` failure |
| H19 | Remote edit reports individual link failures |
| H20 | OTA key auth added to reboot, io/key, syslog, network/config, misc/password |
| SEC-6/H7 | `mIoHome` null guard added to `api_action_post`, `pairing_task`, `learn_task`, `pair_device_task`, `api_upload_devices`, `LinkRemoteToDevice` |
| CPP-3/ESP-1 | `vTaskDelay` in `WIFI_EVENT_STA_DISCONNECTED` handler replaced with `esp_timer_start_once`; reconnect and fallback AP check moved to timer callback |
| CPP-4 | Null guard on `mIoRtsManager` moved before dereference in `SendIoDeviceStatus` |
| M1 | `mIsIoHomePassive` live-reads `IoHomeConfig` instead of cached value |
| M2 | Mutex released before MQTT publish in `SendIoDeviceStatus` |
| M3 | MQTT toggle in-flight guard prevents concurrent save race |
| M4 | Fallback timeout defaults to 600 s when field is blank |
| M5 | `saveNetworkConfig` confirms before rebooting |
| M6 | `UNKNOWN_POSITION` guard before direction comparison |
| M8 | `reloadSettings` uses `Promise.allSettled` with in-flight guard |
| M9 | Manual OTA reloads page after device comes back online |
| M10 | `pollUntilOnline` probes `/api/info` instead of `/api/devices` |
| M11 | `sdkconfig.defaults` corrected to `esp32` target |
| M12 | CI merge-bin passes `--flash-size 4MB` |
| M13 | Syslog error toast uses `(error.message \|\| error)` fallback |
| M14 | `reloadSettings` cancels sniff/learn/pair timers on tab switch |

---

## Open Bugs

---

## 🔴 HIGH — Crashes, data loss, or serious incorrect behaviour

### Security / Authentication

> **Design decision (2026-06-12):** The device is deployed on a trusted home LAN. SEC-1 through SEC-5 are downgraded to **won't fix (trusted LAN)**. If the deployment model changes (e.g. cloud access, public network), these should be revisited.

~~**SEC-1** — `POST /api/wifi/config` unauthenticated~~ — *won't fix (trusted LAN)*
~~**SEC-2 / H8** — `POST /api/upload/devices` and `POST /api/upload/remotes` unauthenticated~~ — *won't fix (trusted LAN)*
~~**SEC-3** — `GET /api/io/key` exposes RF system key unauthenticated~~ — *won't fix (trusted LAN, for now)*
~~**SEC-4** — `GET /api/mqtt` exposes MQTT broker password unauthenticated~~ — *won't fix (trusted LAN)*
~~**SEC-5** — `POST /api/mqtt` unauthenticated~~ — *won't fix (trusted LAN)*

### C++ / FreeRTOS

**CPP-1 — `UpdateDevicesStatusTask` reads `sDeviceMap` without holding `sMutex`**
The task iterates `sDeviceMap` and reads 64-bit timestamp fields before taking `sMutex`. On 32-bit ARM, 64-bit loads are non-atomic — concurrent writes produce torn reads. Concurrent structural writes (CPP-2) also cause iterator invalidation → crash.
`IoHomeControl.cpp:721–774`

**CPP-2 — `AddDevice` and `RestoreDevice` write to `sDeviceMap` without `sMutex`**
Both modify the shared `sDeviceMap` with no synchronisation while `UpdateDevicesStatusTask` may be iterating it and `ProcessReceivedFrameTask` may hold `sMutex`. Concurrent unsynchronised `std::map` mutation is undefined behaviour → crash or silent corruption.
`IoHomeControl.cpp:781–821`

**CPP-3 / ESP-1 — `vTaskDelay` blocks the WiFi system event task for up to 30 seconds**
`WIFI_EVENT_STA_DISCONNECTED` handler calls `vTaskDelay(30 000 ms)` in the `sys_evt` task. Blocks all other WiFi, IP, mDNS, and MQTT reconnect events for the full delay. The fallback AP task started here also races the pending `esp_wifi_connect()`.
`NetworkHelpers.cpp:273–277`

**CPP-4 — Null check after dereference in `SendIoDeviceStatus`**
```cpp
std::lock_guard<std::mutex> guard(mIoRtsManager->mIoDevicesMutex); // dereference
if (mIoRtsManager == nullptr)  // check too late — UB if null
```
The null guard is unreachable and provides no protection.
`MqttHelpers.cpp:1333–1335`

**ESP-2 — Radio operations run synchronously in the MQTT event handler**
`MQTT_EVENT_DATA` dispatches `DiscoverAndPairDevice`, `SetDevicePosition`, `CloseDevice`, etc. directly — blocking the MQTT client task for multiple seconds (sMutex 30 s wait + radio retries). This prevents MQTT keepalives → broker disconnects, and likely overflows the MQTT task's 4 096-byte stack under the resulting call depth.
`MqttHelpers.cpp:333–535`

**JS-1 — `ensureApiModule()` is a no-op; device and remote uploads always 401**
`utils.js` defines `window.MiOpenApi` before `app.js` loads. `app.js`'s `ensureApiModule()` sees it already defined and skips the definition that adds the `X-OTA-Key` header to `uploadFile`. All device/remote JSON uploads therefore hit the server without auth and return 401. OTA uploads in `ota.js` are unaffected (use raw XHR).
`utils.js:130`, `app.js:3, 37`

**JS-2 — WiFi reconnect poll is unbounded — no timeout or failure message**
After `saveWifiConfig`, a `setInterval` polls every 3 s forever. If the new credentials are wrong, the poll never resolves, the toast disappears after 8 s, and the user receives no feedback that the device is unreachable.
`settings.js:120`

### Old High Items Still Open

**H12 — WiFi reconnect poll probes wrong endpoint**
After saving WiFi config, the reconnect poll probes `/api/ota/key` instead of `/api/info`. If that endpoint is ever removed or secured, the page appears permanently stuck.
`settings.js:117–120`

**H13 — `findAssetUrl` uses substring match, not suffix match**
`indexOf(suffix)` matches `firmware.bin.sha256` for suffix `-firmware.bin`. If GitHub adds checksum files alongside binaries, the wrong URL is silently selected.
`updater.js:29`

**H14 — OTA key read from DOM at call time; can be empty string**
If `fetchAndDisplayKey()` hasn't resolved when the user clicks Update, `otaKey` is `""`, the request 401s, and the user sees a cryptic error instead of "OTA key not ready."
`updater.js:109`

**H15 — WebSocket parse errors silently swallowed**
The broad `try/catch` in `ws.onmessage` suppresses not just JSON parse failures but any runtime error in any message handler.
`app.js:242`

**H16 — GitHub API and `/api/info` failures silently swallowed**
Empty `.catch(() => {})` on both fetches. If the device is unreachable at page load, version stays blank and the update checker never runs with no user feedback.
`updater.js:163`, `app.js:336`

**H17 — Dead `MiOpenApi` implementation in `app.js`**
`utils.js` always wins; `app.js`'s version is permanently dead code. Any modification to one diverges from the other silently.
`app.js:3–35`

**H18 / JS-9 — `initIoKeyModal` in `ota.js` targets non-existent HTML IDs**
Looks for `#io-key-modal`, `#io-key-confirm`, etc. — all absent from `index.html`. Silently returns on first line. The function and `fetchAndDisplayIoKey()` are entirely dead code.
`ota.js:161–239`

---

## 🟡 MEDIUM — Significant issues, non-crashing

**SEC-7 — `POST /api/io/sniff` unauthenticated — enables RF key capture**
An attacker can activate sniff mode and trigger pairing via `POST /api/pair/start` (also unauthenticated) then read the captured system key from `GET /api/io/sniff`. Alternative path to SEC-3.
`web_server.cpp:1418`

**SEC-8 — `api_action_post` dereferences `mIoHome` without null guard**
All radio-dispatch branches in `api_action_post` call `s_manager->mIoHome->*()` without null check. `POST /api/action` is unauthenticated; if radio init failed, any action request crashes the device.
`web_server.cpp:481–524`

**CPP-5 — FreeRTOS resource creation failures unchecked in `IoHomeControl` constructor**
`xQueueCreate`, `xEventGroupCreate`, `xTaskCreate` return values are not checked. Null handles used unconditionally in all subsequent code → assertion fault in FreeRTOS on first use.
`IoHomeControl.cpp:398–419`

**CPP-7 — TOCTOU race on `s_capture_timeout_task` handle**
The capture task sets `s_capture_timeout_task = nullptr` then calls `vTaskDelete(nullptr)`. A concurrent `api_capture_start_post` can call `vTaskDelete` on an already-deleted handle → FreeRTOS heap corruption.
`web_server.cpp:2112–2137`

**CPP-8 — Queued status callback can silently revert a `DeactivateDevice` call**
If a device-status queue item was pushed before deactivation, `deviceStatusCallback` processes the stale snapshot and resets `is_deleted` back to `false`, triggers MQTT rediscovery, and overwrites NVS — the device reappears in Home Assistant and revives after reboot.
`IoRtsManager.cpp:103–108`

**ESP-3 — OTA rollback marked valid immediately after `InitHardware()`**
The firmware is marked valid before WiFi, radio, MQTT, and web server have been exercised. A new build that crashes any of those services will never trigger rollback.
`main.cpp:91`

**ESP-4 — `interpolation_timer_cb` acquires `std::mutex` and publishes MQTT from high-priority esp_timer task**
`esp_timer` callbacks run at near-max priority. Blocking on `std::mutex` or `esp_mqtt_client_publish` from there stalls all other pending esp_timer callbacks system-wide (SNTP, MQTT reconnect, confirmation polls).
`IoRtsManager.cpp:165–191`

**ESP-5 — `process_iodevicestatus_task` 4 096-byte stack too small**
The callback chain includes LittleFS write + cJSON MQTT discovery payload + `esp_mqtt_client_publish` loop. Stack overflow silently corrupts adjacent memory.
`IoHomeControl.cpp:403`

**ESP-7 — Device-status and log callback tasks run at priority 0**
Starved under radio/MQTT/WiFi load. `sIoDeviceStatusQueue` and `sLogQueue` fill and drop entries; log already shows `"IoLog can't add log to queue!"` under load.
`IoHomeControl.cpp:39–43`

**JS-3 — "Loading…" indicator not removed on device fetch error**
Error path doesn't clear the loading `<li>`. Device list appears stuck on "Loading…" after any fetch failure.
`devices.js:481`

**JS-4 — 800 ms race in remote capture wizard**
If the user clicks "Enter manually" within 800 ms of a `remote_seen` WebSocket event, the delayed `showStep("arm-step-devices")` overrides their navigation silently.
`remotes.js:302–315`

**JS-5 — Static IP fields submitted without validation**
DHCP-off path sends empty IP/mask/gateway strings to the device with no format check. A blank static IP config breaks device networking and may make it unreachable.
`settings.js:215–236`

**JS-6 — Dimmer slider hardcoded to 0, ignoring current device level**
`makeSlider(app, device, "dim", 0)` — hard-coded initial value. Moving the slider from 0 issues an immediate dim command even when user only wanted to check the current level.
`devices.js:188`

**JS-7 — `retries_boot` and `retries_running` cannot be set to 0**
`parseInt("0") || 3` evaluates to `3`. A user intending 0 retries silently gets 3 saved.
`settings.js:72–73`

**M7 — API response shapes are inconsistent across endpoints**
Three distinct shapes: `{"success":bool}`, `{"status":"..."}`, and HTTP error with plain text. JS uniformly checks `r.success` — status-only responses cause false error toasts throughout.
`web_server.cpp` (multiple handlers)

---

## 🔵 LOW — Minor / maintenance

**SEC-9 / L1 — Non-constant-time `memcmp` on OTA key**
Timing side-channel from LAN. Practically very hard to exploit over TCP, but fix is trivial.
`web_server.cpp:769`

**SEC-10 — Log WebSocket broadcast doesn't escape backslash**
A log line with `\` produces invalid JSON, silently breaking the web UI log panel for all connected sessions.
`web_server.cpp:283`

**SEC-11 — Unauthenticated blocking WiFi scan is a DoS vector**
`esp_wifi_scan_start(..., true)` blocks the httpd task for up to 15 s per call, no auth, no rate limit.
`web_server.cpp:900`

**CPP-9 — Interpolation timer handle not retained; cannot be cancelled**
`esp_timer_handle_t th` is a local — lost after `esp_timer_start_periodic`. Cannot be stopped during `Reboot()`.
`IoRtsManager.cpp:212–217`

**CPP-10 — `expectedResponseID` formally uninitialised (UB) in default switch case**
`ret` stays `false` so it is never read in practice, but the compiler may legally assume the default case is unreachable.
`IoHomeControl.cpp:2196`

**ESP-8 — No FreeRTOS stack overflow checking in sdkconfig**
`CONFIG_FREERTOS_TASK_STACK_OVERFLOW_CHECK` not set in defaults. Stack overflows produce silent heap corruption with no diagnostics.
`sdkconfig.defaults`

**ESP-9 — `nvs_set_*` return values unchecked at multiple sites**
Silent NVS write failures cause configuration to diverge from what the user saved.
Multiple files

**ESP-10 — Non-standard partition table offset 0xF000 undocumented**
Standard is 0x8000. The extra 28 KB gap is unexplained. If not required by a custom bootloader, wastes flash and confuses contributors.
`sdkconfig.defaults`

**ESP-11 — Timer handles in reboot paths created but never deleted**
`wifi_cfg` and `web_reboot` timer handles are leaked before `esp_restart()`. Harmless in normal flow but technically a leak.
`web_server.cpp:821–828, 1352–1359`

**JS-8 — `updateFavButton` hardcodes English tooltip strings**
Tooltip reverts to English after setting a favourite even on NL/DE/FR clients. i18n keys exist for both strings.
`devices.js:163`

**L2 — Calibration task race: two concurrent POSTs both pass the empty-string check**
`s_cal_device_id` is an empty-string guard; two rapid concurrent calls both see `""` before either assigns.
`web_server.cpp`

**L3 — `s_cal_cancel` lost if cancel arrives before task starts**
Flag reset to `false` at task start; a cancel posted before the task runs is silently ignored.
`web_server.cpp`

**L5 — `#net-dhcp-toggle` not declared in `createElements()`**
Inconsistent with all other elements; accessed by direct `document.getElementById` instead.
`settings.js`

**L6 — `ota.js` XHR has no `onabort` handler**
Upload button stays disabled indefinitely if the request is aborted by the browser.
`ota.js`

**L7 — `isNewer` silently ignores 4th version segment**
`v1.2.3.4` compared as `v1.2.3`.
`updater.js`

**L8 — Fallback AP toggle flashes "off" on first paint**
Toggle shows off state before `loadFallbackConfig` resolves.
`settings.js`

**L9 — `sdkconfig.defaults.heltec` redundantly duplicates base defaults**
Maintenance hazard — changes to defaults need applying in two places.
`sdkconfig.defaults.heltec`

**L10 — `i18n:changed` fires device + remote fetches concurrently**
Remote name lookup reads `devicesCache` which may be stale if devices arrive second.
`app.js`

---

## Priority Fix Order

| Priority | ID | Issue |
|---|---|---|
| 1 | CPP-1/2 | Wrap all `sDeviceMap` access in `sMutex` |
| 2 | CPP-3/ESP-1 | Replace `vTaskDelay` in WiFi event handler with `esp_timer` |
| 3 | ESP-2 | Offload MQTT radio commands to a worker task |
| 4 | CPP-4 | Move null guard before dereference in `SendIoDeviceStatus` |
| 5 | JS-2 | Add timeout + failure message to WiFi reconnect poll |
| 6 | H12 | Fix WiFi reconnect poll endpoint (`/api/ota/key` → `/api/info`) |
| 7 | ESP-5 | Increase `process_iodevicestatus_task` stack to ≥ 8 192 bytes |
| 8 | SEC-7/H18 | Add auth to `POST /api/io/sniff`; remove dead `initIoKeyModal` in `ota.js` |
| 9 | M7 | Normalise API response shapes across all endpoints |
