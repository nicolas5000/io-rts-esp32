# Implementation Prompt: Device Add/Remove + Pairing Wizard

## Context

Project: `io-rts-esp32` — ESP32 web portal controlling io-homecontrol devices (shutters, blinds, gates) over a 433MHz radio link using an SX1276 radio module.

**Key existing APIs:**
- `IoRtsManager::RemoveIoDevice(deviceID)` — currently sets `is_deleted=true` in memory AND deletes the NVS file. This will be split into two operations (see Step 1).
- `IoHomeControl::DiscoverAndPairDevice()` — opens a discovery window on the radio and pairs any device that announces itself (blocks until discovered or timeout)
- `deviceStatusCallback(deviceID, device)` in `IoRtsManager.cpp` (lines 46–135) — fires on every device status update; new device detection already saves to NVS
- WebSocket broadcast helpers: `web_server_broadcast_position()`, `web_server_broadcast_log()` in `web_server.cpp`

**Key data structures:**
- `iohome::IoDevice` (in `io-homecontrol/protocol/iohome_device.hpp`) — contains `bool is_deleted` field, already serialised to NVS
- `Helpers::StoredIoDevice` (in `helpers/DeviceStorage.hpp`) — wraps `IoDevice` + `linked_remotes` list; persisted per-device as a JSON file under `/devices/<id>.json`
- `IoRtsManager::mIoDevices` — in-memory map of ALL devices including inactive ones; active devices have `is_deleted == false`

**Relevant files:**
- `components/web_server/web_server.cpp` — HTTP server, API endpoints, WebSocket
- `main/IoRtsManager.cpp` + `main/IoRtsManager.hpp` — manager layer
- `io-homecontrol/IoHomeControl.hpp` — protocol layer API
- `helpers/DeviceStorage.hpp` + `.cpp` — NVS persistence
- `web_data/index.html` — single-page app HTML
- `web_data/js/devices.js` — device list rendering + edit popup
- `web_data/js/app.js` — app wiring
- `web_data/css/style.css` — styling
- `web_data/lang/en.json` (+ nl.json, de.json, fr.json) — i18n strings

**Upload web files** (no full rebuild needed):
```
curl -X POST "http://io-rts-esp32.local/api/upload/web?path=/js/devices.js" \
  -H "X-OTA-Key: 9589d43f076a796bd618be46284a9487" \
  --data-binary "@web_data/js/devices.js"
```

---

## Device lifecycle: inactive vs purged

io-homecontrol devices store pairing state on both sides (ESP32 and the physical motor/screen). Once a device is removed from the ESP32's NVS, the physical device **cannot be re-paired** — it will not respond to `DiscoverAndPairDevice()` because from its perspective the pairing is still active. The physical device needs to be explicitly put back into factory/pairing mode first (which may require the original remote and a specific button sequence that is not always possible).

Therefore the lifecycle has two distinct steps:

| Action | What happens | Reversible? |
|---|---|---|
| **Deactivate** | `is_deleted = true` in NVS, removed from radio layer, shown as inactive in UI | Yes — re-activate restores it |
| **Delete** | NVS file deleted, removed from memory, gone permanently | No — only allowed on already-inactive devices |

The user must deactivate first, then delete. Attempting to delete an active device is rejected with an error: *"Deactivate the device first before deleting."* This two-step requirement prevents accidental permanent removal.

Re-activating a previously deactivated device calls `mIoHome->RestoreDevice()` and clears `is_deleted`, which re-registers it in the radio layer. The physical device still holds the pairing state and will respond normally.

---

## Step 1 — Backend: Deactivate, purge and re-activate

### Changes to `IoRtsManager`

Add three new methods to `IoRtsManager.hpp` / `IoRtsManager.cpp`:

**`DeactivateDevice(deviceID)`**
- Sets `it->second.is_deleted = true` in `mIoDevices`
- Calls `mIoHome->DeleteDevice(deviceID)` to stop radio monitoring
- Saves updated device to NVS via `DeviceStorage::SaveIoDevice()` (keeps the file, `is_deleted: true`)
- Notifies MQTT (same as current `RemoveIoDevice`)
- Broadcasts `{"type":"device_deactivated","id":"..."}` via WebSocket

**`DeleteDevice(deviceID)`**
- Returns an error (logs a warning) if `is_deleted == false` — device must be deactivated first
- Removes from `mIoDevices` entirely
- Calls `DeviceStorage::RemoveIoDevice(deviceID)` to delete NVS file
- Notifies MQTT
- Broadcasts `{"type":"device_deleted","id":"..."}` via WebSocket

**`ReactivateDevice(deviceID)`**
- Sets `it->second.is_deleted = false` in `mIoDevices`
- Calls `mIoHome->RestoreDevice(deviceID, device)` to re-register in radio layer
- Saves updated device to NVS via `DeviceStorage::SaveIoDevice()`
- Notifies MQTT
- Broadcasts `{"type":"device_reactivated","id":"..."}` via WebSocket

### Changes to `LoadIoDevicesFromStorage()`

Load ALL stored devices (active and inactive) into `mIoDevices` at boot, but only call `mIoHome->RestoreDevice()` for those where `is_deleted == false`:

```cpp
for (const auto &[deviceID, storedDevice] : storedDevices) {
    mIoDevicesMutex.lock();
    mIoDevices.insert({deviceID, storedDevice.device});
    mIoDevicesMutex.unlock();
    if (!storedDevice.device.is_deleted) {
        mIoHome->RestoreDevice(deviceID, storedDevice.device);
        for (const std::string &remoteID : storedDevice.linked_remotes)
            mIoHome->LinkRemoteToDevice(remoteID, deviceID);
    }
}
```

### Changes to `GET /api/devices`

Include inactive devices in the response with an `"inactive": true` field. The existing `if (dev.is_deleted) continue;` guard must be removed and replaced with passing through `is_deleted` as the `inactive` flag:

```cpp
cJSON_AddBoolToObject(obj, "inactive", dev.is_deleted);
```

Inactive devices are still returned so the UI can show them in a disabled state with reactivate/purge options.

### Changes to `api_action_post`

Add three new action branches:

```cpp
} else if (strcmp(action, "deactivateDevice") == 0) {
    if (strlen(deviceId) > 0) { s_manager->DeactivateDevice(deviceId); ok = true; }
} else if (strcmp(action, "deleteDevice") == 0) {
    if (strlen(deviceId) > 0) {
        ok = s_manager->DeleteDevice(deviceId);
        if (!ok) send_result(req, false, "Deactivate the device first before deleting.");
    }
} else if (strcmp(action, "reactivateDevice") == 0) {
    if (strlen(deviceId) > 0) { s_manager->ReactivateDevice(deviceId); ok = true; }
}
```

### CLI: Deactivate and delete commands

Extend `helpers/CmdLineIoTools.cpp` with two new commands:

```
deactivate_device --id <deviceID>   Mark device as inactive (keeps NVS, stops radio monitoring)
delete_device --id <deviceID>       Permanently delete an already-inactive device
delete_inactive                     Delete all devices currently marked as inactive
```

`delete_device` refuses with an error message if the target device is still active (not yet deactivated). `delete_inactive` prints the list of inactive devices and asks the user to type `yes` before proceeding. Register all three in `init_cmdline_tools()`.

---

## Step 2 — Backend: Pairing endpoints + WebSocket events

### `POST /api/pair/start`

Starts discovery in a background FreeRTOS task. Responds immediately with `{"status":"started"}` or `{"status":"busy"}` if already active. The task:
1. Sets `s_pairing_active = true`
2. Calls `s_manager->mIoHome->DiscoverAndPairDevice()`
3. On success: broadcasts `{"type":"device_paired","id":"...","name":"...","type_name":"..."}` via WebSocket
4. On failure/timeout: broadcasts `{"type":"pair_failed","reason":"timeout"}`
5. Clears `s_pairing_active = false`

The new device is automatically saved to NVS by the existing `deviceStatusCallback` — no extra storage logic needed.

### `GET /api/pair/status`

Returns `{"active": true/false}` so the UI can recover pairing state after a page reload.

### WebSocket event: `device_added`

Extend `deviceStatusCallback` in `IoRtsManager.cpp` to detect the new-device case and broadcast:
```json
{"type":"device_added","id":"...","name":"...","type_name":"..."}
```
This fires for any newly seen device, regardless of whether the pairing wizard is open.

### Remote capture: "press a button" approach

After pairing succeeds the wizard asks the user to press any button on the remote they want to link. The backend opens a 30-second capture window and intercepts the first inbound frame from an unregistered sender.

**`POST /api/remote/capture/start`**

Sets `s_capture_active = true` and starts a 30-second timeout task. When the radio receive path sees an inbound frame from a node ID not in `mIoDevices` (active or inactive), copy the ID to `s_captured_remote_id` and broadcast:
```json
{"type":"remote_seen","id":"AABBCC"}
```
Then clear `s_capture_active`. On timeout broadcast:
```json
{"type":"remote_capture_timeout"}
```

**`POST /api/remote/capture/cancel`**

Clears `s_capture_active` without broadcasting. Called when user skips or closes the wizard.

**Reuse for adding remotes to existing devices**

This same capture flow is reused from the device edit popup and from the remotes section whenever the user wants to link a new physical remote. The capture → confirm → name → link sequence is implemented once (`remoteCaptureWizard`) and called from multiple entry points.

---

## Step 3 — Frontend: Deactivate, re-activate and purge in device edit popup

In `web_data/js/devices.js`, inside `buildEditPopup()`:

### For active devices

Add a **Deactivate** button (not labelled "Delete" — the wording matters):

```javascript
opts.btnShowDelete = true;  // reuse existing delete slot
opts.deleteLabel = app.i18nText("button.deactivate", "Deactivate");
opts.deleteWarning = app.i18nText("popup.deactivate_warning",
    "The device will be kept in the list as inactive and can be re-activated later. " +
    "Use Purge to permanently remove it.");
opts.onDelete = async function () {
    await window.MiOpenApi.postJson("/api/action", {
        deviceId: freshDevice.id, action: "deactivateDevice"
    });
    app.logStatus(app.i18nText("popup.device_deactivated", "Device deactivated."), "info");
    app.closePopup();
    await fetchAndDisplayDevices(app);
};
```

Also add an **Identify** button so the user can visually confirm which physical device they are deactivating before confirming (existing `IdentifyDevice` API action).

### For inactive devices

When `freshDevice.inactive === true`, show different options in the popup:

- **Re-activate** button — calls `reactivateDevice` action, then refreshes
- **Delete** button — shown with strong red warning:
  *"This permanently removes the device. The physical device cannot be re-paired without putting it into factory/pairing mode first — which may not always be possible."*
  Requires an inline confirmation step (click a second confirm button). The delete button is only visible when the device is already inactive — it does not appear for active devices.

### Device list rendering

Inactive devices are rendered with a visual indicator: grayed-out card, an `inactive` badge next to the type badge, and no control buttons (open/stop/close/slider). The edit button is still shown so the user can re-activate or purge.

Add CSS class `.device.inactive` with reduced opacity and a muted background.

---

## Step 4 — Frontend: Pairing wizard

Add a **Pair Device** button (`+` icon) in the Devices section header alongside the existing help button.

The wizard is a multi-step flow using the existing popup:

### Step 1 — Instructions
Title: *"Pair New Device"*
Text: *"Power the device. Use your existing remote to put it in pairing mode (press the Prog button until the device briefly moves). Then click Start Discovery."*
Button: **Start Discovery** → Step 2

### Step 2 — Scanning
Calls `POST /api/pair/start`. Shows *"Scanning for device…"* with a 60-second countdown. Listens for `device_paired` or `pair_failed` WebSocket events. On timeout or `pair_failed` → show error with **Retry** and **Cancel** buttons.

### Step 3 — Success + link remote offer
On `device_paired`:
- Show *"Device '<name>' paired successfully!"*
- *"Would you like to link a physical remote to this device?"*
- **Link Remote** → Step 4
- **Done** → close wizard, refresh device list

### Step 4 — Remote capture
Calls `POST /api/remote/capture/start`. Shows *"Press any button on the remote you want to link…"* with a 30-second countdown. On `remote_seen`:
- Show captured ID and a name input field
- **Confirm** → `POST /api/action { action: "linkRemote", deviceId, remoteId }` → close wizard
On `remote_capture_timeout` → show **Try again** / **Skip** buttons.
**Cancel** at any time → `POST /api/remote/capture/cancel`.

**Step 4 is also entered standalone** from the device edit popup ("Link Remote" button) and the remotes section, so it is implemented as the reusable `remoteCaptureWizard` module.

---

## Step 5 — WebSocket handling in app.js

```javascript
case "device_added":
    app.fetchAndDisplayDevices();
    app.logStatus("New device detected: " + msg.name, "info");
    break;
case "device_deactivated":
    app.fetchAndDisplayDevices();  // re-render with inactive state
    break;
case "device_reactivated":
    app.fetchAndDisplayDevices();
    break;
case "device_deleted":
    var el = document.querySelector('.device[data-id="' + msg.id + '"]');
    if (el) el.closest('li')?.remove() || el.remove();
    break;
case "device_paired":
    pairingWizard.onPaired(msg);
    break;
case "pair_failed":
    pairingWizard.onFailed(msg.reason);
    break;
case "remote_seen":
    remoteCaptureWizard.onSeen(msg.id);
    break;
case "remote_capture_timeout":
    remoteCaptureWizard.onTimeout();
    break;
```

Encapsulate pairing state in `pairingWizard` and remote-capture state in `remoteCaptureWizard` — both in `devices.js` or a new `pair.js` module.

---

## Step 6 — i18n keys

Add to all four lang files (`en.json`, `nl.json`, `de.json`, `fr.json`):

```
"button.pair_device":            "Pair Device"
"button.deactivate":             "Deactivate"
"button.reactivate":             "Re-activate"
"button.delete":                 "Delete"
"button.start_discovery":        "Start Discovery"
"button.link_remote":            "Link Remote"
"button.skip":                   "Skip"
"button.retry":                  "Retry"
"button.cancel_pairing":         "Cancel"
"popup.pair_step1_title":        "Pair New Device"
"popup.pair_step1_text":         "Put the device in pairing mode using your remote (Prog button), then click Start Discovery."
"popup.pair_step2_scanning":     "Scanning for device…"
"popup.pair_step3_success":      "Device '{name}' paired!"
"popup.pair_step3_link_remote":  "Would you like to link a physical remote to this device?"
"popup.remote_capture_prompt":   "Press any button on the remote you want to link…"
"popup.remote_capture_timeout":  "No remote detected. Try again or skip."
"popup.remote_captured":         "Detected remote {id}. Enter a name and confirm."
"popup.pair_timeout":            "No device found. Check pairing mode and try again."
"popup.deactivate_warning":      "The device will be kept as inactive and can be re-activated later. Use Purge to permanently remove it."
"popup.delete_warning":          "Permanent removal. The physical device cannot be re-paired without putting it into factory/pairing mode first."
"badge.inactive":                "inactive"
```

---

## Test and hold points

### Hold Point 1 — UI only, no hardware (web files only)

Implement Steps 3–6 (frontend + i18n). Deploy via curl. Test entirely in the browser:

- Inject fake WebSocket events from the browser console to walk every wizard step and state:
  ```javascript
  app._wsOnMessage({data: JSON.stringify({type:"device_paired", id:"AABBCC", name:"Test Blind"})})
  app._wsOnMessage({data: JSON.stringify({type:"remote_seen", id:"112233"})})
  app._wsOnMessage({data: JSON.stringify({type:"pair_failed", reason:"timeout"})})
  app._wsOnMessage({data: JSON.stringify({type:"remote_capture_timeout"})})
  ```
- Manually set `device.inactive = true` on a rendered card and verify it looks correct (grayed, no controls, re-activate/purge buttons in edit popup)
- No device is touched. Safe to iterate freely.

### Hold Point 2 — Backend timeout paths, no hardware (firmware flash needed)

Flash the firmware. Test failure/neutral paths without any device interaction:

- `POST /api/pair/start` → wait 60 s → confirm `pair_failed` WebSocket event, wizard shows retry
- `POST /api/remote/capture/start` → wait 30 s → confirm `remote_capture_timeout`
- `POST /api/remote/capture/cancel` mid-window → confirm flag cleared
- `GET /api/pair/status` → `{"active":false}` when idle, `{"active":true}` during scan
- `GET /api/devices` → confirm inactive devices appear with `"inactive":true`

No device deactivated or purged. Safe to iterate.

### Hold Point 3 — Deactivate and re-activate (safe, fully reversible)

Pick any one device. Test the full deactivate → re-activate cycle:

- Open edit popup → click Deactivate → confirm warning text → confirm
- Verify device card becomes grayed/inactive in UI
- Verify device no longer responds to open/close commands (radio layer removed)
- Open edit popup on inactive device → click Re-activate → confirm
- Verify device card returns to active state
- Verify device responds to commands again

No NVS file is deleted. Fully reversible. Repeat as many times as needed.

### Hold Point 4 — Delete (irreversible — do once, when confident)

Only proceed when Hold Point 3 is fully verified. Deactivate one device, then delete it:

- Deactivate the device (Hold Point 3 flow)
- Verify the Delete button now appears in the edit popup (it must NOT appear for active devices)
- Click Delete → read warning → click second confirm button
- Verify device disappears from the list entirely
- Verify `GET /api/devices` no longer returns it
- Verify attempting to delete an active device via the API directly returns the "deactivate first" error

**Do not delete a device unless you have a way to put the physical device back into factory/pairing mode**, or you are prepared to permanently lose control of it.

### Hold Point 5 — Pairing + remote capture (one physical session)

This requires the physical remote. Do it in one sitting:

1. Start pairing wizard → put device in pairing mode → confirm `device_paired` event
2. Click Link Remote → press remote button → confirm `remote_seen`, name and link
3. Verify device appears active in list, remote appears in remotes table

If step 1 times out: nothing lost, try again. If step 2 times out: device is paired, only remote link is missing — linkable later from the device edit popup.

---

## Step 7 — MQTT / Home Assistant integration for device lifecycle

### Current behaviour (keep as-is for deactivate)

`SendIoDevicesDiscovery()` in `MqttHelpers.cpp` (line 748) already sends an **empty discovery payload** for `is_deleted == true` devices — removing the entity from HA entirely. `SendIoDeviceStatus()` (line 1005) clears all retained state/position topics. This existing behaviour is correct and intentional: a deactivated device must not appear in HA as "unavailable" because that is indistinguishable from a connectivity or hardware fault. Keep this as-is.

The problem to solve is purely: **how does a user know which devices are inactive, and how do they reactivate or delete one from HA without opening the web UI?**

### Inactive devices list sensor

Add a new `sensor` component to the **controller's** HA discovery (`SendControllerDiscovery()`). This sensor's state is a human-readable list of currently inactive devices, updated every time the inactive set changes.

**State topic**: `<prefix>/inactive_devices/state`

**Value format**: a semicolon-separated string of `name (ID)` pairs, or `"none"` if all devices are active:
```
Kitchen Blind (AABBCC); Living Room Shutter (112233)
```

Add a helper `PublishInactiveDevicesList()` in `MqttHelpers` that builds this string from `mIoDevices` (all entries where `is_deleted == true`) and publishes it to the state topic with `retain=true`. Call it from `DeactivateDevice()`, `ReactivateDevice()`, and `DeleteDevice()` in `IoRtsManager`, and on MQTT reconnect before `SendDiscovery()`.

Discovery config for the sensor component (added to the controller's `cmps` object):
```json
"inactive_devices": {
  "p": "sensor",
  "unique_id": "inactive_devices",
  "name": "Inactive IO devices",
  "state_topic": "<prefix>/inactive_devices/state",
  "entity_category": "diagnostic",
  "icon": "mdi:eye-off"
}
```

`entity_category: "diagnostic"` keeps it out of the main dashboard and places it in the device's diagnostic section where it belongs.

### Update controller management components

The existing `RemoveIoDevice` text entity (line 635) currently calls `RemoveIoDevice()` (hard delete). Update the MQTT event handler to call `DeactivateDevice()` instead. Rename it to `"Deactivate IO device"`:

```cpp
static const std::string MQTT_CLIENT_DEACT_DEVICE_ID = "DeactivateIoDevice";  // replaces REM_DEVICE_ID
```

### Re-activate: MQTT select entity (dropdown)

Replace the plain `ReactivateIoDevice` text input with a `select` entity. HA renders this as a dropdown — the user picks a device name from the list and the ESP32 reactivates it immediately. No manual ID entry needed.

**Discovery config** (added to controller's `cmps`):
```json
"reactivate_select": {
  "p": "select",
  "unique_id": "reactivate_io_device",
  "name": "Re-activate IO device",
  "options": ["Kitchen Blind (AABBCC)", "Living Room Shutter (112233)"],
  "state_topic":   "<prefix>/reactivate_select/state",
  "command_topic": "<prefix>/reactivate_select/set",
  "entity_category": "config",
  "icon": "mdi:eye-check"
}
```

**Options format**: `"<name> (<ID>)"` — the ID is always the last 6 characters before the closing `)`, so parsing is unambiguous regardless of device name content.

When no inactive devices exist, options must contain at least one entry (HA requires a non-empty list). Use a single placeholder option `"— none —"` which the command handler ignores.

**State topic**: publish the currently selected value back after each selection (HA select requires this). After successfully reactivating, reset the state to `"— none —"` and update the options list.

**Command handler** in `mqtt_event_handler()`: parse the device ID from the selection (extract text between last `(` and `)`), call `ReactivateDevice(id)`, then call `PublishInactiveDevicesList()` and `SendControllerDiscovery()` to refresh the dropdown options.

### Dynamic options: re-publishing controller discovery

MQTT `select` options live in the discovery payload — they cannot be updated via a state topic. Every time the inactive set changes (deactivate, reactivate, delete), re-publish the controller discovery with updated options. Add `SendControllerDiscovery()` calls at the end of `PublishInactiveDevicesList()`. This is acceptable since these events are infrequent.

### Delete: text input with confirmation

Keep `DeleteIoDevice` as a text input requiring `<deviceID>:CONFIRM`. Deletion is destructive and irreversible — a dropdown with immediate action would be too easy to trigger accidentally. The user reads the device ID from either the inactive sensor or the reactivate dropdown, then types `AABBCC:CONFIRM` to delete:

```cpp
static const std::string MQTT_CLIENT_DEL_DEVICE_ID = "DeleteIoDevice";
```

### Resulting HA user experience

| Device state | HA entity | Inactive sensor | Reactivate dropdown |
|---|---|---|---|
| Active | Visible, controllable | Not listed | Not listed |
| Deactivated | Removed from HA | Listed as `Name (ID)` | Listed as option |
| Deleted | Removed from HA | Not listed | Not listed |

Workflow from HA:
1. Device is deactivated from web UI → disappears from HA, appears in sensor and dropdown
2. To reactivate: open controller device page, open "Re-activate IO device" dropdown, select the device → it reappears in HA immediately, dropdown updates
3. To delete: read ID from sensor or dropdown, enter `AABBCC:CONFIRM` in "Delete IO device" text field → permanently removed

### Tests

- Deactivate a device → confirm HA entity removed, inactive sensor updated, device appears in reactivate dropdown
- Deactivate a second device → confirm both appear in sensor and dropdown options
- Select first device from reactivate dropdown → confirm entity reappears in HA, dropdown options update to show only the second device
- When no inactive devices → confirm dropdown shows `"— none —"`, sensor shows `"none"`
- Delete via `DeleteIoDevice` text input with `<id>:CONFIRM` → entity gone, removed from sensor and dropdown
- Attempt `DeleteIoDevice` with plain ID (no `:CONFIRM`) → ignored
- Attempt `DeleteIoDevice` on active device → rejected
- MQTT reconnect → confirm sensor, dropdown options, and discovery all reflect current state correctly

---

## Additional suggestions

### A — Identify before deactivate
In the deactivate confirm step, show an **Identify** button that triggers a brief jog on the physical device so the user can confirm which motor they are deactivating before committing.

### B — Invert open/close
Add a checkbox to the device edit popup: *"Invert open/close direction"* — calls the existing `InvertOpenClosePositionForDevice()` via a new action `"invertOpenClose"`.

### C — Pairing mode visual indicator
While `s_pairing_active` is true, broadcast a periodic `{"type":"pairing_active","remaining_s":N}` heartbeat every 5 seconds. Show a pulsing badge in the header. Useful when multiple browser tabs are open.

---

## Build / flash notes

- Steps 1–2 (C++ backend changes) require `idf.py build` + `idf.py -p COM4 flash`
- Steps 3–6 (web files only) can be uploaded individually via `POST /api/upload/web?path=...` — no flash needed
- OTA key: `9589d43f076a796bd618be46284a9487` — may change if device is re-flashed from scratch; retrieve via `GET http://io-rts-esp32.local/api/ota/key`
