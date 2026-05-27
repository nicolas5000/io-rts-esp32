# Future Improvements — Implementation Prompt

Backlog of planned improvements for io-rts-esp32. Each item is self-contained and can be implemented independently.

The project is an ESP32 project using ESP-IDF v6. The web server runs in `components/web_server/web_server.cpp` using `esp_http_server`. Web UI files (HTML/CSS/JS) live in the `web` LittleFS partition built from `web_data/`. OTA firmware updates use `POST /api/ota` (X-OTA-Key header). Web files can be updated over WiFi via `POST /api/upload/web*?path=<path>` (same key). The device is reachable at `io-rts-esp32.local`.

Work through each item in order, step by step with testing at each step. Do not proceed until the current step is confirmed passing.

---

## Item 1 — Web UI package upload (ZIP)

Allow the user to upload all web UI files in a single `.zip` via the browser, replacing the need to upload files one by one with `POST /api/upload/web`.

**Why**: the web UI spans ~12 files across `js/`, `css/`, `img/`, `lang/`. Uploading them individually is tedious. A ZIP upload lets users apply a web UI release in one step without USB access.

**Binary compatibility**: a `web.zip` downloaded from a GitHub release works identically to a self-built one, as long as the file paths inside the ZIP match the existing layout (`index.html`, `js/app.js`, etc.). The ZIP upload endpoint validates each entry path before writing.

### Backend

- Bundle `miniz.c` and `miniz.h` (single-file public-domain ZIP library, ~60 KB) into a new component or directly into `components/web_server/`. Add it to `CMakeLists.txt` SRCS.
- Add `POST /api/upload/web/package` endpoint (register as `/api/upload/web/package*` to match with query strings if needed), protected by the OTA key.
- The handler streams the request body into a heap buffer (limit: e.g. 256 KB — check the `web` partition size `0x47000` = 284 KB), then uses `mz_zip_reader_init_mem` to open the archive and iterates entries with `mz_zip_reader_get_num_files` / `mz_zip_reader_get_filename` / `mz_zip_reader_extract_to_heap`.
- For each entry: validate the path (no `..`, must not start with `/`), prepend `WEB_BASE_PATH`, `fopen("w")` and write the extracted bytes. Skip directory entries.
- Return `{"status":"ok","files":N}` on success, or HTTP 500 with the failed filename on error.
- Log each written file and total bytes.

### Frontend

- Extend the existing "Firmware Update" card (or add a sibling "Web UI Update" card in the same section) with:
  - File picker (`accept=".zip"`, id `web-pkg-file`)
  - "Upload Package" button (`id="web-pkg-upload"`)
  - Progress bar + status span (same pattern as firmware upload in `ota.js`)
- Use `XMLHttpRequest` with `upload.onprogress` — same approach as firmware upload.
- On 200: show "Done — N files updated. Reload the page to see changes." with a "Reload" button.
- On 413 / 500: show the error body text.

### Tests

- Build a `web.zip` from the current `web_data/` contents (files at root of ZIP, preserving subdirectory structure)
- Upload via the new UI card → confirm 200 and file count matches
- Reload the page → confirm UI is unchanged (same files re-written)
- Upload a ZIP containing a modified `index.html` (add a test comment) → confirm the change is served after reload
- Exception test 1: upload a non-ZIP file (e.g. a `.bin`) → expect 500 with clear error, no files written
- Exception test 2: upload a ZIP with a path traversal entry (`../../nvs`) → expect 400, no files written
- Exception test 3: upload with wrong OTA key → expect 401
- Exception test 4: upload a ZIP larger than the heap limit → expect 413 or 500, device stays up

---

## Item 2 — GitHub Actions release pipeline

Automatically build and publish a GitHub release on every version tag, providing ready-to-use `io-rts-esp32.bin` and `web.zip` artifacts so users can update without a development environment.

### What to implement

- Add `.github/workflows/release.yml` that triggers on `push` to tags matching `v*.*.*`
- Build step: use the `espressif/esp-idf` Docker image (`v6.0.1`) to run `idf.py build`
- Artifact step: collect `build/io-rts-esp32.bin` and create `web.zip` from `web_data/` (zip the contents, not the folder itself)
- Release step: use `softprops/action-gh-release` to create a GitHub release with both artifacts and auto-generated release notes from commits since the previous tag
- The version in the firmware (`esp_app_desc_t.version`) should match the tag — add a `version.txt` containing the tag name, referenced by `CMakeLists.txt` via `set(PROJECT_VER ...)` or `idf_build_set_property(PROJECT_VER ...)`

### Tests

- Push a `v0.1.0` tag → confirm the Actions workflow runs, both artifacts are attached to the release
- Download `io-rts-esp32.bin` from the release → flash via `POST /api/ota` → confirm device reboots and `GET /api/info` returns `"version":"v0.1.0"`
- Download `web.zip` from the release → upload via Item 1 endpoint → confirm page reloads correctly

---

## Item 3 — OTA key visible and copyable in web UI

Currently the OTA key is only visible in the serial log on first boot, making it hard to retrieve on a new machine without USB access. Once the web UI is accessible, the key should be retrievable from it.

**Security consideration**: the key protects OTA flashing. Showing it in the web UI means anyone with network access to the device can retrieve it. This is acceptable for a home-network device but should be noted.

### What to implement

- Add `GET /api/ota/key` endpoint that returns `{"key":"<current_key>"}` — no authentication required (the key is the authentication mechanism itself; if you can reach the device you can already try keys)
- In the "Firmware Update" card, add a small "Show key" link next to the OTA key input. On click, fetch `/api/ota/key` and populate the input field.
- Add a copy-to-clipboard button next to the input.

### Tests

- On a fresh browser session where `localStorage` has no saved key: click "Show key" → field is populated with the correct key
- Confirm the key works for a subsequent OTA flash
- Confirm the key is also accepted by `POST /api/ota` via curl

---

## Item 4 — Fix CMake re-run on every build (missing kconfig option warning)

Every `idf.py build` triggers a full CMake re-configure step (~50 s) instead of a fast incremental ninja build. The root cause is the warning `Missing kconfig option. Re-run the build process...` which forces ninja to re-run CMake.

### Investigation

- Run `idf.py build` twice in a row and confirm the second run still shows `[0/1] Re-running CMake...`
- Identify which Kconfig symbol is "missing": the warning comes from `project.cmake:786`. It fires when a symbol referenced in `sdkconfig.defaults` (or a component's `Kconfig.projbuild`) is not yet present in the generated `sdkconfig` at configure time, requiring a second pass.
- Check if the `CONFIG_WEB_ENABLED` or `CONFIG_OTA_API_KEY` symbols defined in `main/Kconfig.projbuild` and `components/web_server/Kconfig.projbuild` are the source — these are defined after the IDF components configure, which can cause a one-extra-pass cycle.

### Fix options

- If caused by `sdkconfig.defaults` referencing a symbol not yet defined at first configure pass: ensure the symbol's `Kconfig.projbuild` is processed before `project()` is called, or move the symbol to a top-level `Kconfig`.
- If caused by a stale `sdkconfig` not matching `sdkconfig.defaults`: delete `sdkconfig` and let it regenerate from defaults — confirm the warning disappears on second build.
- After fix: run `idf.py build` three times in a row and confirm none of them show `Re-running CMake` after the first.

---

## Item 5 — OLED display on/off control from web UI and console

Allow the user to switch the SSD1306 OLED display on and off without rebooting — useful for power saving or to blank the display in dark environments.

The display is driven by `components/oled_display/`. The current API exposes `oled_init()` and `oled_show_status()`. The SSD1306 supports a hardware display-on/off command (`0xAE` = off, `0xAF` = on) that blanks the panel without losing the framebuffer contents.

### Backend (C++)

- Add two functions to `oled_display.h` / `oled_display.c`:
  - `void oled_set_enabled(bool enabled)` — sends `0xAF` (on) or `0xAE` (off) over I2C, stores the state in a static bool
  - `bool oled_get_enabled(void)` — returns current state
- Guard both functions with `#if CONFIG_OLED_ENABLED` so they compile away when the display is not configured
- Add `GET /api/oled` to the web server returning `{"enabled": true/false}` (omit entirely if `!CONFIG_OLED_ENABLED`)
- Add `POST /api/oled` accepting `{"enabled": true}` or `{"enabled": false}`, calls `oled_set_enabled()`, returns `{"status":"ok"}`
- Persist the state to NVS (namespace `oled`, key `enabled`) so the setting survives reboot — restore it in `oled_init()`
- Add a console command `oled on` / `oled off` / `oled status` via `CmdLineManagement`

### Frontend

- Add an "OLED Display" toggle row in the settings page (only render it if the API returns a non-404 for `GET /api/oled`)
- A checkbox or toggle switch labelled "Display enabled" that calls `POST /api/oled` on change
- Load current state from `GET /api/oled` when the settings page opens (same pattern as syslog)

### Tests

- `POST /api/oled` with `{"enabled": false}` → display goes dark immediately, `GET /api/oled` returns `{"enabled":false}`
- `POST /api/oled` with `{"enabled": true}` → display comes back on showing the last content
- Reboot the device → display state is restored from NVS (if it was off, stays off after boot)
- Console: `oled off` → display off; `oled on` → display on; `oled status` → prints current state
- Web UI toggle: uncheck → display off; check → display on
- Build with `CONFIG_OLED_ENABLED=n` → confirm no compile errors (the API returns 404, the web UI hides the row)

---

## Item 6 — Dynamic log level control (web UI + console)

Allow the ESP log level to be changed at runtime without a reboot — useful for temporarily increasing verbosity during pairing or debugging, then lowering it again to reduce noise.

**Why**: the ESP-IDF log system supports runtime level changes via `esp_log_level_set(tag, level)`. Currently the level is fixed at compile time. During pairing or radio troubleshooting, `io-hctrl` at Debug is very helpful; in normal operation it produces too much output.

### Backend

- Add `GET /api/log/level?tag=<tag>` returning `{"tag":"io-hctrl","level":3}` — reads the current effective level via `esp_log_level_get()` (available in ESP-IDF v5+).
- Add `POST /api/log/level` accepting `{"tag":"io-hctrl","level":4}` (level: 0=none, 1=error, 2=warn, 3=info, 4=debug, 5=verbose). Calls `esp_log_level_set(tag, level)`. Tag `"*"` sets all tags. No authentication required (not security-sensitive).
- Register both in `web_server.cpp` alongside the other `/api/` routes.

### CLI

- Add `helpers/CmdLineLogTools.cpp` with a `log_level` command:
  `log_level --tag <tag> --level <0-5>`
  Calls `esp_log_level_set()` directly. Register it in `init_cmdline_tools()` in `CmdLineManagement.cpp`.

### Frontend

- Add a "Log level" row inside the existing "Syslog settings" card (or a new small card) in the Settings page:
  - Text input for tag (default `*`, hint: `io-hctrl`, `web_server`, …)
  - Dropdown: Error (1) / Warning (2) / Info (3) / Debug (4) / Verbose (5)
  - **Apply** button — calls `POST /api/log/level`
- Load the current level for `*` on settings page open via `GET /api/log/level?tag=*`.
- Note: the level is **not** persisted to NVS — it resets to the compile-time default on reboot. That is intentional: temporary debug sessions should not accidentally persist.

### Tests

- `POST /api/log/level` with `{"tag":"*","level":4}` → confirm debug output appears in the web log panel
- `POST /api/log/level` with `{"tag":"*","level":2}` → confirm only warnings/errors appear
- `POST /api/log/level` with `{"tag":"io-hctrl","level":4}` → only `io-hctrl` tag becomes verbose, others unaffected
- CLI: `log_level --tag web_server --level 4` → web_server debug lines appear in serial output
- Reboot device → level reverts to compile-time default (confirm with `GET /api/log/level?tag=*`)
- Web UI: change dropdown to Debug and click Apply → `GET /api/log/level?tag=*` returns `{"level":4}`

---

---

## Item 7 — Smooth position tracking with transit-time interpolation

During a movement command the physical device takes 20–60 seconds to travel its full range. The ESP32 only learns the final position when the device reports back after stopping. Until then the UI slider stays frozen at the last known position, making it impossible to see progress and giving no feedback that the command was received.

**Why**: io-homecontrol devices do not stream position continuously — they report back once on arrival (or periodically via CMD 0x71 `STATUS_UPDATE` if configured). Real-time feedback during movement requires client-side prediction based on the known travel time of each device.

### Concept

Each device has a characteristic **transit time** — the time in milliseconds to travel from 0 % to 100 % (fully open to fully closed). Once this is known for a device, position during movement can be linearly interpolated:

```
estimated_position = start_position + (target_position - start_position)
                     × (elapsed_ms / transit_time_ms)
```

The estimate is clamped to the target and replaced by the real position as soon as the device reports back. Transit times vary by device type and installation (heavier screens travel slower) so they must be calibrated per device, not hardcoded.

### Data model changes

Add `transit_time_ms` to `Helpers::StoredIoDevice` (default 0 = uncalibrated). Serialise and deserialise it in `DeviceStorage` alongside the existing device fields.

Add transient movement-tracking fields to `iohome::IoDevice` (not persisted):
- `int64_t move_start_us` — `esp_timer_get_time()` when the last position command was sent
- `float move_start_pos` — position at the time the command was sent
- `float move_target_pos` — commanded target position

### Transit time calibration

Calibration is automatic and passive — no separate calibration command is needed:

1. When a full-range command is sent (0 → 100 or 100 → 0) and the device later reports arrival at the target with `is_stopped == true`, compute `elapsed = now - move_start_us`.
2. Store `elapsed` as the new `transit_time_ms` for that device (or apply a weighted average with any previous measurement to smooth out noise).
3. For partial movements, scale: `transit_time_ms = elapsed / abs(target - start) × 100`.
4. Save the updated value to NVS via `DeviceStorage::SaveIoDevice()`.

Expose calibration state in `GET /api/devices` as `"transit_time_ms": 42000` (0 if unknown). The web UI can show a small indicator (e.g. a clock icon) when transit time is not yet calibrated for a device.

### Interpolation task

Add a periodic timer in `IoRtsManager` (or in `IoHomeControl`) that fires every 1 second while any device `is_stopped == false` and `transit_time_ms > 0`. For each such device compute the estimated position and call `web_server_broadcast_position()` with an added `estimated` flag:

```cpp
void web_server_broadcast_position(const char *device_id, int position,
                                   bool is_stopped, bool estimated);
```

The WebSocket payload gains a field:
```json
{"type":"position","id":"...","position":47,"stopped":false,"estimated":true}
```

When `is_stopped` becomes true and the real position arrives, broadcast once more with `"estimated":false` to snap the slider to the confirmed value.

### Frontend

- The position slider in `devices.js` already responds to `position` WebSocket events via `updateDeviceFill()`. No structural change is needed — just handle the new `estimated` field.
- While `estimated == true`, render the slider with a dashed or semi-transparent track to signal it is a prediction, not a confirmed reading.
- When the confirmed position arrives (`estimated == false`), snap the slider and remove the visual indicator.
- Show transit time in the device edit popup as read-only info: *"Transit time: 42 s"* (or *"Not yet calibrated"*).

### Dedicated calibration tool

For installations where automatic calibration is impractical (e.g. a device that is never commanded to do a full-range movement in normal use), provide an explicit calibration wizard accessible from the device edit popup via a **Calibrate** button.

**Procedure** (two full traversals to measure both directions and average them):

1. Send `open` command (position 0 %). Begin polling `ForceDeviceStatusUpdate()` every 2 s. Wait until reported position ≈ 0 and `is_stopped == true` — the device is now at its fully open position.
2. Record `t0 = esp_timer_get_time()`. Send `close` command (position 100 %). Resume polling every 2 s.
3. Wait until reported position ≈ 100 and `is_stopped == true`. Record `t1`. `transit_close_ms = (t1 - t0) / 1000`.
4. Record `t2`. Send `open` command (position 0 %). Resume polling every 2 s.
5. Wait until reported position ≈ 0 and `is_stopped == true`. Record `t3`. `transit_open_ms = (t3 - t2) / 1000`.
6. `transit_time_ms = (transit_close_ms + transit_open_ms) / 2`. Save to NVS. Broadcast `{"type":"calibration_done","id":"...","transit_time_ms":42000}`.

Polling via `ForceDeviceStatusUpdate()` rather than relying on the device's spontaneous `STATUS_UPDATE` (0x71) makes the procedure reliable across all device types — some devices only report back when explicitly queried. Use a fixed 2-second poll interval throughout; calibration is a deliberate short user-initiated action so the extra radio traffic is acceptable regardless of device power type.

The full procedure takes approximately 2–4 minutes depending on the device. It can be cancelled at any time; if cancelled after step 3, the single-direction measurement is saved as a provisional value.

**Backend — `action: "calibrate"`**

Add a new action branch in `api_action_post`. Starts a FreeRTOS task that executes the procedure above. Uses a module-level flag `s_calibrating_device_id` to prevent two calibrations running simultaneously. Broadcasts progress events:

```json
{"type":"calibration_progress","id":"...","step":1,"message":"Moving to open position…","position":72}
{"type":"calibration_progress","id":"...","step":2,"message":"Measuring close travel…","position":35}
{"type":"calibration_progress","id":"...","step":3,"message":"Measuring open travel…","position":61}
{"type":"calibration_done","id":"...","transit_time_ms":42000}
{"type":"calibration_failed","id":"...","reason":"timeout"}
```

The `position` field in each progress event carries the latest polled position so the UI can animate the slider in real time during calibration. A step times out if `is_stopped` is not reached within 120 s (hard cap, regardless of any prior transit time estimate).

**Backend — `action: "cancelCalibration"`**

Sets a cancel flag that the calibration task checks between steps. The device is left at whatever position it reached; the partial measurement is saved if at least one direction completed.

**Frontend — calibration wizard in device edit popup**

The **Calibrate** button opens a modal step display inside the existing popup (reusing the popup content area):

- Shows current step description and a progress indicator (step 1 of 3, step 2 of 3 …)
- Live position slider updates during travel (driven by the normal `position` WebSocket events)
- On `calibration_done`: show *"Calibration complete — transit time: 42 s"* with a **Close** button
- On `calibration_failed`: show the reason with a **Retry** and **Cancel** button
- **Cancel** button visible throughout — calls `cancelCalibration` action

Only available for device groups that support position control (shutter, venetian, window). The button is hidden for gates, switches, and dimmers.

### Manual transit time override

Some devices may not tolerate a full automated calibration (e.g. a device near its physical limits). Add a read-only info line in the device edit popup: *"Transit time: 42 s"* (or *"Not yet calibrated"*), with a small **Edit** link that opens an inline number input. Saving writes to NVS via action `"setTransitTime"` accepting `value` in seconds.

### Tests

- Send a close command (0 → 100) to a shutter: confirm estimated position WebSocket events arrive every second and the slider moves during travel
- Confirm the slider snaps to the real position when `is_stopped` becomes true
- After calibration, confirm `GET /api/devices` returns a non-zero `transit_time_ms`
- Send a partial movement (30 → 70): confirm interpolation stays within bounds and does not overshoot
- Set transit time manually via the edit popup: confirm the value is persisted across reboot
- Device with `transit_time_ms == 0` (uncalibrated): confirm no estimated events are sent, slider only updates on real reports

---

## Item 7a — Real-time position feedback to WebUI and MQTT using transit time

Depends on Item 7 (transit time known per device) and the calibration tool (Item 7 calibration section).

**Why**: Once transit time is calibrated, the ESP32 can calculate the expected position at any moment during movement and push that to both the WebSocket (web UI slider) and MQTT (Home Assistant or other automation systems). This avoids MQTT consumers seeing a stale position for the entire duration of a movement. At the calculated stop moment the real position is confirmed with a single targeted poll rather than continuous polling throughout the move.

### Position broadcast during movement

When a position command is sent for a device with a known `transit_time_ms`, start a 1-second periodic timer in `IoRtsManager`. On each tick, compute:

```
elapsed_ms      = (esp_timer_get_time() - move_start_us) / 1000
fraction        = elapsed_ms / transit_time_ms          (clamped 0.0–1.0)
estimated_pos   = start_pos + (target_pos - start_pos) × fraction
```

Broadcast via both channels:
- WebSocket: `{"type":"position","id":"...","position":47,"stopped":false,"estimated":true}`
- MQTT: publish to the device's existing position topic with the estimated value

Mark the MQTT message as `retain: false` so that only confirmed final positions are retained in the broker. Estimated in-flight values are ephemeral.

Stop the timer when `elapsed_ms >= transit_time_ms` (clamp reached) — no point continuing to broadcast the same clamped value.

### Scheduled confirmation poll

Rather than polling continuously or waiting indefinitely for a spontaneous device report, schedule a single `ForceDeviceStatusUpdate()` at:

```
confirmation_time = move_start_us + transit_time_ms × distance_fraction × 1000 + CONFIRM_OFFSET_US
```

Where `CONFIRM_OFFSET_US` is a fixed 3-second offset (tunable via `sdkconfig` or a `#define`) to give the device time to physically stop before being queried. A device that is still decelerating at the calculated stop time will have settled by the time the poll arrives.

When the real position response arrives:
- Stop the interpolation timer if still running
- Broadcast the confirmed position to WebSocket with `"estimated":false`
- Publish the confirmed position to MQTT with `retain: true` (overwriting the in-flight estimated values)
- Update `transit_time_ms` with the actual elapsed time as per the passive auto-calibration logic in Item 7

If the device auto-reports (spontaneous `STATUS_UPDATE` 0x71) before the scheduled poll fires, cancel the scheduled poll and use the real value immediately — no redundant query needed.

### Background polling for third-party remote control

The transit-time confirmation poll only fires when the ESP32 itself issued a command. A physical remote not registered in the system can move a device without the ESP32 observing the command — the position then silently becomes stale with no interpolation timer and no scheduled poll.

The existing `UpdateDevicesStatusTask()` in `IoHomeControl` already implements a background polling loop using `next_status_update_timestamp` on each `IoDevice`. This mechanism must be **preserved and not disabled** by the transit-time work. It acts as the safety net for all externally-triggered movements.

Ensure the following behaviour is maintained or improved:

- Each device has a configurable **background poll interval** (e.g. 60 s for mains-powered devices). After each confirmed position update the next poll is scheduled at `now + interval`.
- When the ESP32 itself sends a command, the background poll for that device is rescheduled to fire at the confirmation time (`calculated_stop + offset`) from Item 7a — this avoids a redundant background poll firing during or just after a commanded movement.
- If the background poll detects a position change that the ESP32 did not command (position differs from last known by more than a small tolerance), log it and broadcast the update to WebSocket and MQTT as a confirmed position — this is how externally-triggered movements become visible in the UI and automation systems.
- Poll interval can differ by device power type: mains devices every 60 s, low-power (`is_low_power == true`) devices at a longer interval (e.g. 300 s) to avoid draining batteries with unnecessary wake-ups. Unlike during calibration, this background polling runs indefinitely so power impact matters.

### Fallback when transit time is unknown

If `transit_time_ms == 0` (not yet calibrated): no estimated broadcasts are sent. Schedule the confirmation poll at a fixed fallback offset of 60 seconds after the command — a conservative upper bound that ensures the device has stopped even without knowing its transit time. This means the first movement after a fresh install still gets a final-position update, just without live interpolation.

### MQTT topic behaviour summary

| Phase | Value | Retained |
|---|---|---|
| Before movement | Last confirmed position | Yes |
| During movement (estimated) | Interpolated value, updating every 1 s | No |
| After confirmation poll | Real position from device | Yes |

### Tests

- Send a close command to a calibrated device: confirm MQTT receives estimated position updates every second during travel, then a final retained value after the confirmation poll
- Confirm the WebUI slider animates during movement and snaps at confirmation
- Cancel a movement mid-way (send stop command): confirm the interpolation timer stops, confirmation poll fires shortly after, real position is published
- Uncalibrated device: confirm no estimated MQTT messages are sent, confirmation poll fires at 60 s fallback, real position is published with retain
- Device that auto-reports before the confirmation poll: confirm no duplicate poll is sent and the real value is published immediately
- Move a device with a physical remote (not registered): confirm the background poll detects the changed position within one poll interval and publishes the updated value to WebSocket and MQTT
- Confirm that a background poll scheduled during a commanded movement is deferred to the confirmation time and does not fire mid-movement

---

---

## Item 8 — Store current position as MY (favorite) on physical device

Allow the user to reprogram the physical device's MY position from HA or the web UI, without needing the physical remote.

**Background**: the existing MQTT `_fav` button and the web UI "Set as ★" button both do *different* things that are not this:
- MQTT `_fav` button: sends the device to its currently stored MY position (equivalent to pressing the MY button on the physical remote).
- Web UI "Set as ★": saves the current position to browser `localStorage` as a UI shortcut — nothing is sent to the device.

This item is about reprogramming the MY position on the *physical device* itself so it remembers a new MY target.

**Protocol research needed first**: on Somfy io-homecontrol, storing a new MY position from a controller (not a physical remote) is believed to use `CMD_SET_CONFIG1 = 0x6F` (authentication needed), but the exact payload bytes are not yet documented. A Wireshark/radio capture session sniffing a Tahoma box while the user holds the MY button on a remote until the device jogs is needed to recover the bytes before implementation can begin.

### What to implement (once protocol bytes are known)

- Add `StoreFavoritePosition(deviceID)` to `IoHomeControl` — sends `CMD_SET_CONFIG1` with the captured payload.
- Add `action: "storeFavoritePosition"` in `api_action_post` in `web_server.cpp`.
- MQTT: add a new `_set_fav` button entity in `MqttHelpers.cpp` (distinct from the existing `_fav` "go to MY" button), subscribing to a new `/store_fav_pos` topic.
- Web UI: rename "Set as ★" in the device edit popup from a localStorage action to a real device command; update the i18n keys and log messages accordingly.
- HA: the new `_set_fav` button entity appears on the device page in HA alongside the existing "Go to MY" button.

### Tests

- Move device to a position, press "Store as MY" → hold MY button on physical remote → device should jog to confirm the new MY was accepted.
- Press "Go to MY" (existing `_fav` button) → device moves to the newly stored MY position, not the old one.
- Reboot the ESP32 → MY position is still the newly stored one (it is stored in the device's own memory, not in NVS).

After each item: show the test commands run, their output, and explicitly state PASS or FAIL before asking whether to continue to the next item.
