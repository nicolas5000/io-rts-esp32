# IO-Homecontrol Tilt Protocol

## Tilt-Capable Device Types

From INFO2 response (`device_type` field):

| Type | ID | Tilt |
|------|-----|------|
| VENETIAN_BLIND | 0x01 | Yes |
| BLIND | 0x0A | Likely |
| EXTERNAL_VENETIAN_BLIND | 0x11 | Confirmed |
| LOUVRE_BLIND | 0x12 | Likely |

---

## Tilt Value Encoding

Tilt is encoded as **closed percentage** of `0xC800`:

```
tilt_hex = (100 - open_percent) * 0xC800 / 100
```

| Open % | Hex  |
|--------|------|
| 0 (closed) | C800 |
| 25 | 9600 |
| 50 | 6400 |
| 75 | 3200 |
| 100 (open) | 0000 |

---

## Sending a Tilt Command (EXECUTE_REQUEST, cmd 0x00)

```
CTRL0 = 50, payload 8 bytes:
  01 E7 D4 00 20 TT TT 00

  D4 00 = target position value or marker (D2 = stop, D4 = unknown/do nothing)
  20    = tilt parameter flag
  TT TT = tilt value (closed %, big-endian)
```

Compare with position-only command:

```
CTRL0 = 4E, payload 6 bytes:
  01 E7 PP PP 00 00

  PP PP = target position (0x0000=open, 0xC800=closed)
```

---

## Status Request Differences

| Query | Data | Response CTRL0 | Response size | Includes tilt |
|-------|------|----------------|---------------|---------------|
| Basic | `030000` (3 bytes) | 96 | 14 bytes | Bytes [12-13] (may be zero) |
| Tilt-aware | `03200100` (4 bytes) | 98 | 16 bytes | Bytes [13-14] (reliable) |

Use `03200100` for tilt-capable devices to get the full tilt value.

---

## PRIVATE_RESPONSE Formats (cmd 0x04)

### 14-byte (CTRL0=96)

Returned after EXECUTE_REQUEST or from basic `030000` query.

```
[0]    status byte (bit 0 = stopped)
[1]    flags (bit 7 = STATUS_EXPECTED)
[2-3]  target position or marker (e.g. D2 00 = stopped at current, D4 00 = keep position for tilt)
[4-5]  current position
[6-7]  timer/unknown
[8-10] originator node ID
[11]   01
[12-13] tilt (closed %) — unreliable (often 0000 after commands), do not use for tilt extraction
```

When `byte[2] == 0xD2`: device stopped at current position (also seen on non-tilt-capable devices). When `byte[2] == 0xD4`: keep position (used during tilt operations). Bytes [2-3] are always treated as a target position value; marker values (D2, D4) simply exceed the valid position range.

### 16-byte (CTRL0=98)

Returned in response to `03200100` on tilt-capable devices. This is the only reliable source for tilt values.

```
[0]    status byte (bit 0 = stopped)
[1]    flags
[2-3]  target position or marker (e.g. D2 00, D4 00, etc.)
[4-5]  current position
[6-7]  unknown
[8-10] originator node ID
[11]   01
[12]   20 (tilt flag)
[13-14] tilt (closed %)
[15]   00
```

Tilt decoding: `tilt_open_percent = 100 - (bytes[13-14] * 100 / 0xC800)`
