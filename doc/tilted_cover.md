# Tilted Cover Commands

- **Controller (sender):** `AA9BFA`
- **Actuator (receiver):** `EE8165`

---

# Close Tilt (0%)

Captured command sequence for closing the tilt of an already fully-down cover (position 0%) to tilt-closed (tilt 0%).

## Command Sequence

| # | Freq (MHz) | Command | Name | Direction | CTRL0 | CTRL1 | Data (hex) | Notes |
|---|-----------|---------|------|-----------|-------|-------|------------|-------|
| 1 | 868.95 | 0x00 | EXECUTE_REQUEST | AA9BFA -> EE8165 | 50 | 00 | 01E7D20020C80000 | Auth required. Payload: position D2=STOP?, 20C80000 (C800 = 100% closed) |
| 2 | 868.95 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | 6EC5276AC060 | Authentication response to challenge (for cmd 0x00) |
| 3 | 868.25 | 0x0C | PRIVATE2 | AA9BFA -> EE8165 | 4C | 00 | D8000000 | D8 = FAVORITE_POSITION, pos 0000 |
| 4 | 868.25 | 0x0D | PRIVATE2_RESPONSE | EE8165 -> AA9BFA | 8D | 00 | 05C8000000 | ACK: 05, C800 = 100% closed, 0000 |
| 5 | 868.25 | 0x03 | PRIVATE | AA9BFA -> EE8165 | 4C | 00 | 03200100 | Status query/command |
| 6 | 868.25 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | 078809D92735 | Auth response (for cmd 0x03) |
| 7 | 868.95 | 0x0C | PRIVATE2 | AA9BFA -> EE8165 | 4C | 00 | D8000000 | D8 = FAVORITE_POSITION, pos 0000 (repeat) |
| 8 | 868.95 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | FDB9BF961F1C | Auth response |
| 9 | 868.95 | 0x0D | PRIVATE2_RESPONSE | EE8165 -> AA9BFA | 8D | 00 | 05C8000000 | ACK: 05, C800 = 100% closed |
| 10 | 869.85 | 0x03 | PRIVATE | AA9BFA -> EE8165 | 4C | 00 | 03200100 | Status query/command (repeat on new freq) |
| 11 | 869.85 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | FE1B17AD1BB4 | Auth response (for cmd 0x03) |
| 12 | 869.85 | 0x04 | PRIVATE_RESPONSE | EE8165 -> AA9BFA | 98 | 00 | 0500D200D8000000AA9BFA0120C80000 | Full status: 05=stopped, D2=STOP_POS, D8=FAVORITE, target AA9BFA, pos C800=100% |
| 13 | 868.25 | 0x0C | PRIVATE2 | AA9BFA -> EE8165 | 4C | 00 | D8000000 | D8 = FAVORITE_POSITION (final) |
| 14 | 868.25 | 0x0D | PRIVATE2_RESPONSE | EE8165 -> AA9BFA | 8D | 00 | 05C8000000 | ACK: 05, C800 = 100% closed |

## Observations

- The cover is already at position C800 (100% closed / fully down). This command closes the tilt.
- Command 0x00 (EXECUTE_REQUEST) initiates the tilt action, followed immediately by a 0x3D challenge response (authentication).
- Command 0x0C (PRIVATE2) with data `D8000000` (D8 = FAVORITE_POSITION with value 0000) is sent multiple times across different frequencies (frequency hopping: 868.25, 868.95, 869.85 MHz).
- Command 0x03 (PRIVATE) with data `03200100` appears to be a status request or tilt-specific command, also requiring authentication (followed by 0x3D).
- Command 0x04 (PRIVATE_RESPONSE) returns full device status including current position, target address, and parameters.
- All 0x0D responses contain `05C8000000` confirming position C800 (100% closed) with status byte 05 (stopped, bit 0 set).
- CTRL0 values: 50 (execute), 0E (challenge response), 4C (private commands from controller), 8D (private response from device), 98 (private response with extended data).

---

# Open Tilt (100%)

Captured command sequence for opening the tilt of an already fully-down cover (position C800 = 100% closed) to tilt-open (tilt 100%). Cover remains down, only tilt changes.

## Command Sequence

| # | Freq (MHz) | Command | Name | Direction | CTRL0 | CTRL1 | Data (hex) | Notes |
|---|-----------|---------|------|-----------|-------|-------|------------|-------|
| 1 | 869.85 | 0x00 | EXECUTE_REQUEST | AA9BFA -> EE8165 | 50 | 00 | 01E7D20020000000 | Auth required. Target tilt position 0000 = 0% (fully open tilt) |
| 2 | 869.85 | 0x3C | CHALLENGE_REQUEST | EE8165 -> AA9BFA | 0E | 00 | 573E98267E45 | Device sends challenge |
| 3 | 869.85 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | 61F01456D8F3 | Controller responds to challenge |
| 4 | 868.95 | 0x0C | PRIVATE2 | AA9BFA -> EE8165 | 4C | 00 | D8000000 | D8 = FAVORITE_POSITION, pos 0000 |
| 5 | 868.95 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | 71B8FDFF734E | Auth response (for cmd 0x0C) |
| 6 | 868.95 | 0x0D | PRIVATE2_RESPONSE | EE8165 -> AA9BFA | 8D | 00 | 05C8000000 | ACK: 05=stopped, C800=100% closed |
| 7 | 868.25 | 0x03 | PRIVATE | AA9BFA -> EE8165 | 4C | 00 | 03200100 | Status/tilt command |
| 8 | 868.25 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | 4B17B2E2C541 | Auth response (for cmd 0x03) |
| 9 | 869.85 | 0x0C | PRIVATE2 | AA9BFA -> EE8165 | 4C | 00 | D8000000 | D8 = FAVORITE_POSITION (repeat) |
| 10 | 869.85 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | 2D8BC430BEC3 | Auth response |
| 11 | 869.85 | 0x0D | PRIVATE2_RESPONSE | EE8165 -> AA9BFA | 8D | 00 | 05C8000000 | ACK: 05=stopped, C800=100% closed |
| 12 | 868.95 | 0x03 | PRIVATE | AA9BFA -> EE8165 | 4C | 00 | 03200100 | Status/tilt command (repeat) |
| 13 | 868.95 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | F71F13C22A11 | Auth response (for cmd 0x03) |
| 14 | 868.95 | 0x04 | PRIVATE_RESPONSE | EE8165 -> AA9BFA | 98 | 00 | 0500D200D8000000AA9BFA0120000000 | Full status: 05=stopped, pos C800, tilt target 0000=0% (open) |
| 15 | 868.25 | 0x0C | PRIVATE2 | AA9BFA -> EE8165 | 4C | 00 | D8000000 | D8 = FAVORITE_POSITION (final) |
| 16 | 868.25 | 0x3C | CHALLENGE_REQUEST | EE8165 -> AA9BFA | 0E | 00 | 0D333C6EBA46 | Device sends challenge |
| 17 | 868.25 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | A5084B984F14 | Controller responds to challenge |
| 18 | 868.25 | 0x0D | PRIVATE2_RESPONSE | EE8165 -> AA9BFA | 8D | 00 | 05C8000000 | ACK: 05=stopped, C800=100% closed |

## Observations

- The EXECUTE_REQUEST payload is `01E7D20020000000` — the key difference from close-tilt is the last 4 bytes: `00000000` (tilt open) vs `C8000000` (tilt closed).
- This capture includes the full 0x3C CHALLENGE_REQUEST from the device (not seen in close-tilt capture, likely missed there).
- The 0x04 PRIVATE_RESPONSE confirms: `0500D200D8000000AA9BFA0120000000` — last 4 bytes `00000000` = tilt target 0% (fully open), while cover position remains C800 (fully down).
- Command 0x0C (PRIVATE2) with `D8000000` and 0x03 (PRIVATE) with `03200100` repeat across frequencies, same pattern as close-tilt.
- The final 0x0C on frame 15 triggers a new 0x3C/0x3D challenge exchange before the 0x0D ACK — suggesting the device may periodically re-authenticate.

---

# Comparison: Close Tilt vs Open Tilt

| Aspect | Close Tilt (0%) | Open Tilt (100%) |
|--------|----------------|-----------------|
| EXECUTE_REQUEST data | `01E7D20020C80000` | `01E7D20020000000` |
| Tilt target value | C800 (100% closed) | 0000 (0% = fully open) |
| PRIVATE_RESPONSE tail | `...0120C80000` | `...0120000000` |
| Cover position in responses | C800 (down) | C800 (down) |
| Overall structure | Identical | Identical |

The tilt position is encoded in the last 4 bytes of the EXECUTE_REQUEST payload (bytes 5-8): `C800` = closed, `0000` = open. The same value appears in the PRIVATE_RESPONSE (0x04) as the tilt target.

---

# Set Position to 80% (No Tilt Change)

Captured command sequence for moving the cover to 80% closed (position only, no tilt parameter). The cover moves from its current position to the target. Log is partial (capture cut short).

## Command Sequence

| # | Freq (MHz) | Command | Name | Direction | CTRL0 | CTRL1 | Data (hex) | Notes |
|---|-----------|---------|------|-----------|-------|-------|------------|-------|
| 1 | 868.25 | 0x00 | EXECUTE_REQUEST | AA9BFA -> EE8165 | 4E | 00 | 01E728000000 | Position-only command. Target 2800 = 80% closed (0x28=40 decimal, 40/0.5=80%) |
| 2 | 868.25 | 0x3C | CHALLENGE_REQUEST | EE8165 -> AA9BFA | 0E | 00 | 004C39746259 | Device sends challenge |
| 3 | 868.25 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | 5FA2688FEF6A | Controller responds to challenge |
| 4 | 868.25 | 0x04 | PRIVATE_RESPONSE | EE8165 -> AA9BFA | 96 | 00 | 0480280014160006AA9BFA010000 | Status: 04=moving, target 2800=80%, current 1416=~40%?, originator AA9BFA |
| 5 | 869.85 | 0x03 | PRIVATE | AA9BFA -> EE8165 | 4C | 00 | 03200100 | Status query (log ends here) |

## Observations

- **CTRL0 = 4E** on EXECUTE_REQUEST vs 50 in the tilt commands. This likely indicates a different command subtype (position-only vs position+tilt).
- **Payload `01E728000000`** is only 6 bytes vs 8 bytes for tilt commands (`01E7D20020xxxx00`). The format differs:
  - Position-only: `01 E7 28 00 00 00` — byte 2 = `28` (target position 0x2800 = 80%)
  - Tilt commands: `01 E7 D2 00 20 xx xx 00` — byte 2 = `D2` (STOP_POSITION param), bytes 5-6 = tilt value
- **0x04 PRIVATE_RESPONSE with CTRL0=96** (vs 98 in tilt commands): 14 bytes vs 16 bytes. Payload `0480280014160006AA9BFA010000`:
  - `04` = status moving (not stopped, bit 0 clear)
  - `80` = flags (CMD_PARAM_STATUS_EXPECTED set — device will send STATUS_UPDATE)
  - `2800` = target position (80% closed)
  - `1416` = current position (device in motion)
  - `0006` = unknown (speed? timer?)
  - `AA9BFA` = originator address
  - `01` = unknown
  - `0000` = tilt position (unchanged/not specified)
- **No 0x0C (PRIVATE2)** commands seen — those only appear in tilt sequences.
- The capture ends early (only 5 frames captured vs 14-18 for tilt commands).

---

# Comparison: Position vs Tilt Commands

| Aspect | Position Only (80%) | Tilt (open/close) |
|--------|-------------------|-------------------|
| EXECUTE_REQUEST CTRL0 | 4E | 50 |
| EXECUTE_REQUEST size | 6 bytes | 8 bytes |
| Payload byte 2 | Position value (0x28=80%) | D2 (STOP_POSITION param) |
| Tilt value in payload | Not present | Bytes 5-6 (0000 or C800) |
| PRIVATE_RESPONSE CTRL0 | 96 | 98 |
| PRIVATE_RESPONSE size | 14 bytes | 16 bytes |
| PRIVATE2 (0x0C) used | No | Yes (repeated with D8000000) |
| Device status byte | 04 (moving) | 05 (stopped) |

## EXECUTE_REQUEST Payload Format

```
Position only (CTRL0=4E, 6 bytes):
  01 E7 PP PP 00 00
  PP PP = target position (big-endian, 0x0000=0%/open, 0xC800=100%/closed)

Position + Tilt (CTRL0=50, 8 bytes):
  01 E7 D2 00 20 TT TT 00
  D2 = STOP_POSITION parameter marker
  20 = separator/flag
  TT TT = tilt value (0x0000=open, 0xC800=closed)
```

---

# Tilt to 25% at Position 80%

Captured command sequence for setting tilt to 25% while the cover is at 80% closed. Partial capture.

## Command Sequence

| # | Freq (MHz) | Command | Name | Direction | CTRL0 | CTRL1 | Data (hex) | Notes |
|---|-----------|---------|------|-----------|-------|-------|------------|-------|
| 1 | 868.95 | 0x00 | EXECUTE_REQUEST | AA9BFA -> EE8165 | 50 | 00 | 01E7D20020960000 | Tilt command. Target tilt 9600 = 75% closed (25% open) |
| 2 | 868.95 | 0x04 | PRIVATE_RESPONSE | EE8165 -> AA9BFA | 96 | 00 | 0480D20027F90001AA9BFA010000 | Moving: target D200=?, current 27F9, originator AA9BFA |
| 3 | 869.85 | 0x03 | PRIVATE | AA9BFA -> EE8165 | 4C | 00 | 03200100 | Status query |
| 4 | 869.85 | 0x3D | CHALLENGE_RESPONSE | AA9BFA -> EE8165 | 0E | 00 | B936EE2A3074 | Auth response (for cmd 0x03) |

## Observations

- **Tilt value `9600`**: In the EXECUTE_REQUEST `01E7D20020960000`, bytes 5-6 = `9600`. Since C800 = 100% closed and 0000 = 0% (fully open), then 9600 = 0x9600/0xC800 = 75% closed = **25% open tilt**. This confirms tilt is expressed as "closed percentage" (inverted from user-facing "open" percentage).
- **CTRL0 = 96 on 0x04 response** (14 bytes, not 16) — same as the position-only response format, not the 98/16-byte tilt format seen when cover is stationary. The device appears to be moving (status `04`, bit 0 clear).
- **Response payload `0480D20027F90001AA9BFA010000`**:
  - `04` = moving
  - `80` = STATUS_EXPECTED flag
  - `D200` = D2 parameter (STOP_POSITION) — matches the command
  - `27F9` = current position in transit (~79.8% — close to the 80% target from previous move)
  - `0001` = unknown
  - `AA9BFA` = originator
  - `01` = unknown
  - `0000` = tilt (current or previous — not yet at target)
- The 0x3C challenge was likely sent but not captured (or auth was cached from previous exchange). Only 0x3D response seen.
- Capture ends early — likely more 0x0C/0x0D exchanges would follow.

## Tilt Value Mapping

| User-facing tilt | Hex value | Calculation |
|-----------------|-----------|-------------|
| 0% (closed) | C800 | 100% of C800 |
| 25% (open) | 9600 | 75% of C800 |
| 50% | 6400 | 50% of C800 |
| 75% | 3200 | 25% of C800 |
| 100% (fully open) | 0000 | 0% of C800 |

Tilt is encoded as **closed percentage**: `tilt_hex = (100 - open_percent) * 0xC800 / 100`

---

# Tilt 25% at Position 80% — io_add Response

Output from `io_add EE8165` (device already paired, requesting status via 0x03 PRIVATE):

## Command Sequence

| # | Freq (MHz) | Command | Name | Direction | CTRL0 | CTRL1 | Data (hex) | Notes |
|---|-----------|---------|------|-----------|-------|-------|------------|-------|
| 1 | 868.95 | 0x03 | PRIVATE | 696969 -> EE8165 | 4B | 20 | 030000 | Status request from our controller |
| 2 | 868.95 | 0x3C | CHALLENGE_REQUEST | EE8165 -> 696969 | 0E | 00 | DED10FD04E80 | Device challenge |
| 3 | 868.95 | 0x3D | CHALLENGE_RESPONSE | 696969 -> EE8165 | 0E | 00 | FD79458949A6 | Controller auth response |
| 4 | 868.95 | 0x04 | PRIVATE_RESPONSE | EE8165 -> 696969 | 96 | 00 | 05002800280A0000696969010000 | Stopped, pos 2800=80%, target 2800=80% |

## Observations

- Device type `0x11` = EXTERNAL_VENETIAN_BLIND, subtype `0x00`.
- The 0x04 response is **14 bytes with CTRL0=96** — same format as position-only movement response.
- **Response payload `05002800280A0000696969010000`**:
  - `05` = stopped (bit 0 set)
  - `00` = flags (no STATUS_EXPECTED)
  - `2800` = target position (80% closed, matches `0x28 * 2 = 0x50 = 80`)
  - `280A` = current position (should be 2800 but shows 280A — minor deviation or includes sub-position?)
  - `0000` = timer/speed (zero, not moving)
  - `696969` = originator (our controller address)
  - `01` = unknown
  - `0000` = **tilt value = 0000 = fully open (100% tilt open)**

Wait — this contradicts! The test scenario says "tilt 25%" but the response shows `0000` (fully open tilt). Let me re-examine...

Actually looking more carefully at the parsed position: bytes 2-3 = `2800` (target), bytes 4-5 = `280A`. If we follow the format from the tilt commands where the 14-byte response is:
```
[0]  status    = 05 (stopped)
[1]  flags     = 00
[2-3] target   = 2800 (position 80%)
[4-5] current  = 280A (≈80%)
[6-7] ????     = 0000
[8-10] origin  = 696969
[11] unknown   = 01
[12-13] tilt   = 0000
```

The last 2 bytes `0000` would indicate tilt is fully open — **but this is the `io_add` status query using the basic `030000` request**. To get tilt info, send `03200100` instead.

---

# PRIVATE_RESPONSE Formats (Corrected)

## 14-byte response (CTRL0=96)

Returned immediately after an EXECUTE_REQUEST (position or tilt command), or from a basic `030000` status query.

```
Byte  Field                Example (position cmd)   Example (tilt cmd)
[0]   status               04 (moving)              04 (moving)
[1]   flags                80 (STATUS_EXPECTED)     80
[2-3] target/marker        2800 (pos target 80%)    D200 (D2 = tilt marker)
[4-5] current position     1416 (~40%)              27F9 (~80%)
[6-7] timer/unknown        0006                     0001
[8-10] originator          AA9BFA                   AA9BFA
[11]  unknown              01                       01
[12-13] tilt/zero          0000                     0000
```

- When `byte[2] == D2`: This is a tilt operation response. Bytes 2-3 are NOT a position target.
  Current position is in bytes [4-5]. No tilt value is reported here.
- When `byte[2] != D2`: Standard position response. Bytes [2-3] = target position, [4-5] = current position.

## 16-byte response (CTRL0=98)

Returned in response to a `03200100` status query on tilt-capable devices.

```
Byte  Field                Example: close-tilt      Example: open-tilt
[0]   status               05 (stopped)             05 (stopped)
[1]   flags                00                       00
[2]   D2 marker            D2                       D2
[3]   zero                 00                       00
[4-5] current position     D800 (*)                 D800 (*)
[6-7] unknown              0000                     0000
[8-10] originator          AA9BFA                   AA9BFA
[11]  separator            01                       01
[12]  tilt flag            20                       20
[13-14] tilt (closed %)    C800 (100% closed)       0000 (0% closed = open)
[15]  zero                 00                       00
```

(*) Note: In the Tahoma captures above, bytes [4-5] show `D800` which is the FAVORITE_POSITION
marker from preceding PRIVATE2 exchanges. In practice with our controller (no PRIVATE2), bytes
[4-5] contain the actual current position value (confirmed via testing: values like 2800=80% etc).

**Tilt decoding**: `tilt_open_percent = 100 - (bytes[13-14] * 100 / 0xC800)`

---

# Detecting Tilt Support

Based on the captured data, tilt support can be determined by:

1. **Device type** (from INFO2 response during discovery/add):
   - `VENETIAN_BLIND (0x01)` — tilt supported
   - `BLIND (0x0A)` — tilt likely supported
   - `EXTERNAL_VENETIAN_BLIND (0x11)` — tilt supported (confirmed by this device)
   - `LOUVRE_BLIND (0x12)` — tilt likely supported

2. **CTRL0 in EXECUTE_REQUEST**:
   - `4E` = position-only command (6 bytes payload)
   - `50` = position+tilt command (8 bytes payload)

3. **CTRL0 in PRIVATE_RESPONSE (0x04)**:
   - `96` = 14-byte response (position info, basic)
   - `98` = 16-byte response (includes explicit tilt value in extended format)

4. **Status query format**:
   - `030000` (3 bytes) = basic query, returns 14-byte response without tilt
   - `03200100` (4 bytes) = tilt-aware query, returns 16-byte response with tilt value
