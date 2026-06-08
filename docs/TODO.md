# io-rts-esp32 — Feature Backlog

Items are independent and can be picked up in any order.

---

## 1W device support

See full spec: `docs/superpowers/specs/2026-06-06-1w-support-design.md`

Adds simplex (1W) io-homecontrol pairing and control as an alternative to 2W. Phases: data model → crypto → packet construction → radio TX → pairing + position tracking → API → web UI.

---

## Web UI — In-app device help

Add a contextual help panel or modal in the web UI covering common physical operations users need to perform on their devices. The panel should be accessible from a help icon (?) on the device card or in the Settings page.

### Somfy IO — Hard reset procedure
A hard reset unpairs the motor from all controllers and resets it to factory defaults. Required when the device stops responding or needs to be moved to a new system.

**Standard Somfy IO hard reset (most motors):**
1. Ensure the motor is stopped and not moving.
2. Press and hold the **PROG** button on the motor head (or on the Telis/Situo remote programmed as the master remote) for 10 seconds until the motor makes a **jog movement** (brief up-down twitch).
3. Release. The motor is now factory reset — all paired controllers are erased.
4. To re-pair: start pairing mode on the motor (short PROG press) then run "Add Device" in this app.

**Note:** Exact procedure varies by motor model. Consult the motor's installation guide if the above does not produce the jog response.

### Somfy IO — Pairing mode (for 2W)
1. Short-press the **PROG** button on the motor head. The motor jogs once to confirm.
2. Within 10 seconds, run "Add Device" in this app — the app sends a discovery broadcast.
3. Motor jogs again to confirm pairing.

### Somfy IO — Setting the MY (favorite) position
1. Move the device to the desired position using Open/Close/Stop.
2. Once at the target position, press and hold the **MY** button on the remote (or use "Set Favorite" in the app if supported) for 2 seconds until the motor jogs.
3. The MY position is now stored on the motor.

### Velux IO — Hard reset
1. Locate the reset button (usually a small recessed button on the motor unit, accessible with a pen tip).
2. Press and hold for 5 seconds until the window/blind makes a brief movement.
3. The device is now unpaired. Re-pair via "Add Device".

### General — Device not responding
- Verify the device is within radio range (~10 m through walls).
- If the device is solar/battery-powered, check the LOW_POWER flag is shown in the device detail — this requires a longer preamble and the app handles it automatically once the device type is known.
- Try "Force Status Update" from the device menu to trigger a fresh poll.
- If the device still does not respond, perform a hard reset and re-pair.

---

## Web UI package upload (ZIP)

Allow uploading all web UI files as a single `.zip` via the browser, replacing individual file uploads. A `web.zip` from a GitHub release would be uploadable in one step.

Backend: stream ZIP into heap buffer, extract each entry to LittleFS via a new `POST /api/upload/web/package` endpoint (OTA key protected, miniz single-file library).

Frontend: file picker for `.zip` + progress bar in the OTA section of Settings.

---

## OLED — On/off control

Add a console command and web UI toggle (Settings → Display) to turn the OLED on or off at runtime. Useful for saving power or reducing screen burn-in. Persist the preference to NVS.

---

## Dynamic log level

Add a `loglevel <tag> <level>` console command and a web UI control (Settings → Logging) to change ESP log levels per tag at runtime without reflashing. Uses `esp_log_level_set()`.

---

## Store current position as MY on device

Send the io-homecontrol SET_FAVORITE command (CMD 0x00, param 0xD8 written back to device) so the MY button on physical remotes also reflects the position set from the app.

---

## Remote support syslog

Opt-in, anonymous diagnostic reporting. When enabled, the device sends structured log lines to a configurable remote syslog server (already implemented) plus optionally to a community endpoint. All identifiers are hashed before transmission.

---

## Toast feedback for commands from any source

Show a toast notification in the web UI whenever a device moves — regardless of whether the command came from the web UI, a physical remote, MQTT, or a WebSocket client. Currently toasts only appear for commands sent from the web UI itself.

Source: the WebSocket already broadcasts `position` events; toasts need to be triggered on incoming `position` events where `is_stopped == false` and the command originated externally.

---

## ~~MQTT — Enable/disable toggle~~ ✓ done (v1.1.5)

Toggle in Settings → MQTT to disable the MQTT client without clearing config. Persisted in NVS. Exposed via `GET /api/mqtt` (`enabled` field) and `POST /api/mqtt`. Web UI toggle wired next to the MQTT header.

---

## Node.js 24 action upgrades

`actions/checkout@v4` and `softprops/action-gh-release@v2` will stop working on September 16, 2026 when GitHub removes Node.js 20 from runners. Upgrade both actions to Node.js 24 compatible versions before that date.

