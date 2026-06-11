# io-rts-esp32 — Claude Code Context

## Working style

Run `curl -s` and `python3 -c` commands against the workbench (`piworkbench.local`) or the device (`io-rts-esp32.local`, COM4, RFC2217) directly without asking for permission. These are routine access commands in this project.

---

## Environment

| Item | Value |
|------|-------|
| Device mDNS | `io-rts-esp32.local` |
| Device COM port | `COM4`, baud `115200`, password `spaargaren` |
| RFC2217 serial | `rfc2217://192.168.178.218:4002` (use .218 Ethernet — .217 WiFi times out) |
| Workbench | `http://piworkbench.local:8080`, device on **SLOT2** |
| Our node ID | `A1B1C3` |

**Active devices (in NVS):**

| Address | Name | Notes |
|---------|------|-------|
| `5DA31C` | Screen_Gijs | |
| `33303C` | Luifel Tuin | |
| `8D794B` | Screen_Tom_Tuin | |
| `70D78E` | Screen_Tom_Huis | Not responding — off or out of range |

**Restoring devices via `POST /api/upload/devices`:**
Send a multipart form upload with a JSON array. The `node_id` field must be the **device's own address** (same as `id`) — it is used as the CMD 0x03 destination. Do NOT set it to our controller node ID (`A1B1C3`).
```bash
curl -s -X POST "http://192.168.178.57/api/upload/devices" \
  -H "X-OTA-Key: <key>" \
  -F "file=@devices.json;type=application/json"
```
Example `devices.json`:
```json
[
  {"id":"5DA31C","name":"Screen_Gijs","node_id":"5DA31C","is_low_power":false},
  {"id":"33303C","name":"Luifel Tuin","node_id":"33303C","is_low_power":false},
  {"id":"8D794B","name":"Screen_Tom_Tuin","node_id":"8D794B","is_low_power":true}
]
```

**io-homecontrol system key:** `E8CD451E4F5D86C9A411E84B769FBFBD`
Read/set via serial: `io_config --read` / `io_config --key <32-hex-chars>`

**OTA key:** retrievable at runtime via `GET http://io-rts-esp32.local/api/ota/key`
Retrievable at runtime via `GET http://io-rts-esp32.local/api/ota/key` (no auth required).
OTA flash: `curl -s -X POST http://io-rts-esp32.local/api/ota -H "X-OTA-Key: <key>" -H "Content-Type: application/octet-stream" --data-binary @build/io-rts-esp32.bin`
Web file upload: `curl -s -X POST "http://io-rts-esp32.local/api/upload/web?path=/<file>" -H "X-OTA-Key: <key>" --data-binary @web_data_v2/<file>`
**Always edit and upload from `web_data_v2/` — `web_data/` is the old UI and is not built or served.**

---

## Build & flash

Use PowerShell with the v6.0.1 Espressif profile — this is the only method that works (Bash/MinGW is rejected by ESP-IDF):

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"
idf.py build 2>&1 | Select-String -Pattern 'error:|Linking' -CaseSensitive:$false
```

Flash:
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"
idf.py flash -p COM4
```

If flash fails with "COM4 busy / PermissionError(13)", kill python processes holding the port:
```powershell
Get-Process | Where-Object { $_.Name -like "python*" } | Stop-Process -Force
```

---

## Serial access rules

**`idf.py monitor` does not work** — it requires an interactive TTY and hangs. Use Python serial scripts instead.

**DTR=True resets the device** — the DTR pin is wired to EN/RESET. Setting `setDTR(True)` fires the reset circuit, display goes dark.

- For **live output** (device already running): open serial, call `setDTR(False)` only — never toggle back to True.
- For **boot-log capture**: the False→True toggle is acceptable and expected (device will reset intentionally).
- Never add a DTR toggle to a "check serial for errors" script after an HTTP test — it will kill the device mid-test.

**Connect without resetting:**
Open with `DTR=False, RTS=False`. Send `\r` (CR, not LF). Console replies with `io-rts-esp32>` if already logged in. Use `tools/console_probe.py` in the repo — it handles all of this automatically.

**Capture live serial output (no reset):**
```powershell
python -c "
import serial, time
s = serial.Serial('COM4', 115200, timeout=0.1)
s.setDTR(False)
time.sleep(0.1)
data = b''
for _ in range(80):
    chunk = s.read(512)
    if chunk: data += chunk
    time.sleep(0.05)
s.close()
open('C:/tmp/liveout.txt', 'wb').write(data)
"; Get-Content C:/tmp/liveout.txt -Encoding UTF8
```

**Capture boot log (resets device):**
```powershell
python -c "
import serial, time, sys
s = serial.Serial('COM4', 115200, timeout=0.2)
s.setDTR(False); time.sleep(0.05); s.setDTR(True)
s.flushInput()
start = time.time(); data = b''
while time.time() - start < 7:
    chunk = s.read(512)
    if chunk: data += chunk
s.close()
open('C:/tmp/bootlog.bin', 'wb').write(data)
text = data.decode('utf-8', errors='replace')
for line in text.splitlines():
    if any(k in line.lower() for k in ['oled','init','error','boot','ready']):
        sys.stdout.buffer.write((line+'\n').encode('utf-8'))
" > C:/tmp/bootlog_filtered.txt 2>&1
Get-Content C:/tmp/bootlog_filtered.txt
```

---

## io-homecontrol protocol findings

### Battery status (CMD 0x00)
- `MainParam=0x0006` and `0x0009` are **position commands** on Somfy devices, NOT battery queries.
  - They coincide with near-zero positions on the 0–51200 (0xC800) scale — sending them moves the device.
- `MainParam=0xA601` (extended power-supply params) → CMD 0xFE error 0x58 (not supported) on all devices.
- Battery data on RS100 SOLAR IO is likely reported autonomously via **CMD 0x71 (STATUS_UPDATE)**, not queryable on demand.
- Do not send CMD 0x00 with MainParam < 0x0010 without knowing the device — it will cause small position movements.

### CMD 0x56 / CMD 0x6A probe (2026-05-10)
- **0x56 (CMD_GET_GENERAL_INFO2):** Devices only respond during initial pairing when `device_type == UNKNOWN`. After pairing they ignore it.
- **0x6A:** Not defined in the io-homecontrol protocol (`iohome_constants.h`). All devices silently ignore it.
- **CMD 0x03 (CMD_PRIVATE):** Works fine — this is the regular status poll.

### SendRaw priority fix
`SendRaw` in `IoHomeControl.cpp` was missing `vTaskPrioritySet(NULL, IO_FRAME_PROCESSING_TASK)` before `SendAndReceive`. Battery-powered devices have a short response window after authentication — without elevated priority the final CMD 0x04 response was missed. Fix: add priority elevation/restore around `SendAndReceive` in `SendRaw` (same pattern as `SetDevicePosition`, `IdentifyDevice`, etc.).

### Pairing sequence
See `docs/pairing_sequence_reference.md` for the full reference including observed sessions, timing, bugs found in `DiscoverAndPairDevice`, and the correct flow for receiving TaHoma's system key.
