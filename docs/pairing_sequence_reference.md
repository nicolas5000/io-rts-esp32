# io-homecontrol Pairing Sequence Reference

## Overview

Pairing is how a controller (e.g. TaHoma/Connectivity Kit) and an io-homecontrol device
exchange the system key. Once both sides share the key, all subsequent communication is
authenticated with it.

## TaHoma "Add Device" Mode — Observed Behavior

**Important**: When pairing is initiated from the TaHoma side (pressing "Add device" in the
app), TaHoma does NOT start broadcasting immediately. It waits for a signal from the device
first. The device sends its programming signal (CMD 2E / CMD 28), and only then does TaHoma
respond with CMD 2C to confirm and proceed with the key exchange.

---

## Protocol Flow (Controller → Device pairing)

| Step | CMD  | Direction              | Data                    | Notes                                     |
|------|------|------------------------|-------------------------|-------------------------------------------|
| 1    | `2E` | Controller → `00003F`  | 1 byte: `00`            | Discovery2E broadcast, repeated ~5×/1 s   |
| 2    | `28` | Controller → `00003B`  | 0 bytes                 | Discovery broadcast, 3 channels × 3 reps  |
| 3    | `2A` | Controller → `00003B`  | 12 bytes (nonce/token)  | SPE discovery, repeated 3×               |
| 4    | `29` | Device → Controller    | 9 bytes (see below)     | Discovery response from device            |
| 5    | `2C` | Controller → Device    | 0 bytes                 | Discovery confirmation (can be sent 2×)   |
| 6    | `2D` | Device → Controller    | 0 bytes                 | Confirmation ack                          |
| — |      | Controller keeps broadcasting CMD 28 for ~4 s while scanning for more devices | | |
| 7    | `31` | Controller → Device    | 0 bytes                 | Key init transfer (~4 s after CMD 2D)     |
| 8    | `3C` | Device → Controller    | 6 bytes (HMAC challenge)| Challenge request                         |
| 9    | `32` | Controller → Device    | 16 bytes (AES key)      | Encrypted system key transfer             |
| 10   | `33` | Device → Controller    | 0 bytes                 | Key transfer confirmation                 |

After key exchange, controller queries device info: CMD 36/37 (node ID), 50/51 (name),
54/55 (info1), 56/57 (info2), 58/59 (info3), 46/47 (config), 03/04 (status).

---

## CMD 29 (Discovery Response) — Data Layout

9 bytes total:

| Byte(s) | Field        | Encoding                                          |
|---------|--------------|---------------------------------------------------|
| 0       | Device type  | `device_type >> 2`  (upper 6 bits)               |
| 1       | Device type  | `(device_type & 0x03) << 6`  (lower 2 bits)      |
| 2–4     | Node ID      | Own node ID repeated in data                     |
| 5       | Manufacturer | `0x02` = Somfy                                    |
| 6       | Padding      | `0xDC`                                            |
| 7       | Padding      | `0x00`                                            |
| 8       | Counter      | Auto-incrementing `uint8_t`                      |

**Decoding by TaHoma**: `type = (data[0] << 2) | (data[1] >> 6)`, `subtype = data[1] & 0x3F`

**Device type values** (from `iohome_constants.h`):

| Type value | Name                | data[0] | data[1] |
|-----------|---------------------|---------|---------|
| `0x02`    | ROLLER_SHUTTER      | `0x00`  | `0x80`  |
| `0x04`    | WINDOW_OPENER       | `0x01`  | `0x00`  |

---

## CMD 2A (SPE Discovery) — Data Observed

CMD 2A is broadcast to `00003B` alongside CMD 28 and contains 12 bytes of nonce/token data
that differs each pairing session:

```
Session 2026-06-10 05:28: B4A686C4D00205BDE789FF3C
Session 2026-06-10 05:43: 1A62261B22879D3F88BA733C
```

---

## Observed Pairing Session — June 10 2026

Controller node **27ECE9** = **TaHoma** (verified: 07:43 local / 05:43 UTC matches the
broadcast pattern).

**Important observation**: TaHoma was triggered via "Discovery" mode, NOT via "Add device" /
"Learn" mode. This means TaHoma sent CMD 2E/28/2A discovery broadcasts as a passive scan —
it was not actively waiting to complete a full key exchange. This explains why CMD 2C was
never sent back to our device: TaHoma only sends CMD 2C when it is in explicit "Add device"
mode, not during a general discovery scan.

Our device (A1B1C3) was running `PairAsDevice` with node ID **204AED** for this session.

### Session 1 — 05:28 UTC

| Timestamp           | Direction | CMD  | From   | To     | Data                       |
|---------------------|-----------|------|--------|--------|----------------------------|
| 2026-06-10T05:28:12 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:28:13 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:28:15 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:28:16 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:28:17 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:28:17 | Rcvd      | `28` | 27ECE9 | 00003B | *(0 bytes)*                |
| 2026-06-10T05:28:18 | Rcvd      | `28` | 27ECE9 | 00003B | *(0 bytes)*                |
| 2026-06-10T05:28:20 | Rcvd      | `28` | 27ECE9 | 00003B | *(0 bytes)*                |
| 2026-06-10T05:28:22 | Rcvd      | `2A` | 27ECE9 | 00003B | `B4A686C4D00205BDE789FF3C` |
| 2026-06-10T05:28:23 | Rcvd      | `2A` | 27ECE9 | 00003B | `B4A686C4D00205BDE789FF3C` |
| 2026-06-10T05:28:25 | Rcvd      | `2A` | 27ECE9 | 00003B | `B4A686C4D00205BDE789FF3C` |
| 2026-06-10T05:28:18 | **Sent**  | `29` | 204AED | 27ECE9 | `0200204AED02DC0004` (868.95 MHz) |
| 2026-06-10T05:28:20 | **Sent**  | `29` | 204AED | 27ECE9 | `0200204AED02DC0005` (868.25 MHz) |

**Result**: CMD 2C was never received — pairing did not complete.

> **Note on CMD 29 data**: The data bytes `02 00` represent an older/buggy encoding.
> The correct encoding for ROLLER_SHUTTER (`0x02`) is `00 80` (data[0]=type>>2, data[1]=(type&3)<<6).
> Source code fix is in `iohome_commands.cpp` — verify binary was compiled with the fix.

### Session 2 — 05:43 UTC

| Timestamp           | Direction | CMD  | From   | To     | Data                       |
|---------------------|-----------|------|--------|--------|----------------------------|
| 2026-06-10T05:43:35 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:43:36 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:43:37 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:43:38 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:43:40 | Rcvd      | `2E` | 27ECE9 | 00003F | `00`                       |
| 2026-06-10T05:43:40 | Rcvd      | `28` | 27ECE9 | 00003B | *(0 bytes)*                |
| 2026-06-10T05:43:41 | Rcvd      | `28` | 27ECE9 | 00003B | *(0 bytes)*                |
| 2026-06-10T05:43:42 | Rcvd      | `28` | 27ECE9 | 00003B | *(0 bytes)*                |
| 2026-06-10T05:43:44 | Rcvd      | `2A` | 27ECE9 | 00003B | `1A62261B22879D3F88BA733C` |
| 2026-06-10T05:43:46 | Rcvd      | `2A` | 27ECE9 | 00003B | `1A62261B22879D3F88BA733C` |
| 2026-06-10T05:43:48 | Rcvd      | `2A` | 27ECE9 | 00003B | `1A62261B22879D3F88BA733C` |

**Result**: No CMD 29 sent — `PairAsDevice` did not respond (likely session had expired or was
not running).

---

## Successful Pairing Reference — May 24 2026 (TaHoma BB0007 → Device 524249)

Captured from sniffer. This is the complete sequence from CMD 28 through key exchange:

```
19:23:49  28  BB0007 → 00003B  (0 bytes)         [discovery broadcast]
19:23:50  29  524249 → BB0007  040052424902DC0B5C [discovery response, type=WINDOW_OPENER]
19:23:50  2C  BB0007 → 524249  (0 bytes)         [discovery confirmation]  ← ~331ms after 29
19:23:50  2D  524249 → BB0007  (0 bytes)         [confirmation ack]
19:23:50  28  BB0007 → 00003B  (0 bytes)         [TaHoma continues scanning]
19:23:52  28  BB0007 → 00003B  (0 bytes)
19:23:53  28  BB0007 → 00003B  (0 bytes)
19:23:54  31  BB0007 → 524249  (0 bytes)         [key init]  ← ~4s after 2D
19:23:54  3C  524249 → BB0007  148F705D8115       [challenge]
19:23:54  32  BB0007 → 524249  198B0BF955ADC9CAD2CA1DD312208E45  [key transfer]
19:23:55  33  524249 → BB0007  (0 bytes)         [key confirmation]
19:23:55  36  BB0007 → 524249  (0 bytes)
19:23:55  37  524249 → BB0007  524249
19:23:55  50  BB0007 → 524249  (0 bytes)         [get name]
19:23:55  51  524249 → BB0007  0053554E454120696F0000000000D800
19:23:55  54  BB0007 → 524249  (0 bytes)         [get info1]
19:23:55  55  524249 → BB0007  353136343636354130340300FFFF
19:23:55  56  BB0007 → 524249  (0 bytes)         [get info2]
19:23:55  57  524249 → BB0007  353135303832324130370400030B0000
```

### Key Timing Observations

| Gap                     | Duration  |
|-------------------------|-----------|
| CMD 28 → CMD 29         | < 100 ms  |
| CMD 29 → CMD 2C         | ~330 ms   |
| CMD 2D → CMD 31         | ~4000 ms  |
| CMD 31 → CMD 3C         | ~10 ms    |
| CMD 3C → CMD 32         | ~17 ms    |
| CMD 32 → CMD 33         | ~6 ms     |

The ~4 second gap between CMD 2D and CMD 31 is critical: TaHoma keeps broadcasting CMD 28
during this window to discover additional devices. Any code waiting for CMD 31 must skip these
CMD 28/2A/2E broadcasts or it will time out prematurely.

---

## ESP as Controller — "Pair" Button + TaHoma "Key Send" (2026-06-11)

### Web UI button roles

| Button | Function | ESP role | Sends CMD 28? |
|--------|----------|----------|---------------|
| Pair | `DiscoverAndPairDevice` | Controller | Yes — ESP sends CMD 28 |
| Learn | `LearnKeyFromController` | Device | No — waits for TaHoma CMD 28 |
| Pair as device | `PairAsDevice` | Device | No — waits for TaHoma CMD 28 |

### Observed (broken) sequence — 17:24 UTC, device 5a9e5456

```
ESP    → 28   112233 → 00003B  (0 bytes)
TaHoma → 29   27ECE9 → 112233  FFC027ECE902CC0000
ESP    → 31   112233 → 27ECE9  (0 bytes)   ← CMD 2C/2D skipped
TaHoma → 3C   27ECE9 → 112233  5B895628EA30
ESP    → 32   112233 → 27ECE9  68BF69F0E645F312D93B5BF069E595A3  (×3)
              ← no CMD 33 from TaHoma → ERROR "failed to send key / no or bad answer received!"
              → loops back to CMD 28
```

### Root cause (confirmed from iown-homecontrol reference repo)

**`DiscoverAndPairDevice` works correctly for normal screens** — screens have no system key yet and simply accept whatever key the controller pushes via CMD 31 → 3C → 32. CMD 2C/2D is not required.

**TaHoma is a controller in key-send mode.** The reference defines two distinct 2W key exchange paths:

**Path 1 — CMD 38 "Launch Key Transfer" (receiver asks sender to share its key):**
```
New device → 38   TaHoma  (launch key transfer, includes 6-byte challenge)
TaHoma     → 32   device  (TaHoma's system key, encrypted with TRANSFER_KEY)
New device → 3C   TaHoma  (challenge — prove you know the key you just sent)
TaHoma     → 3D   device  (HMAC response using the shared key)
```

**Path 2 — CMD 31 "Ask Challenge" (sender pushes its key to receiver):**
```
Controller → 31   device  (ask for challenge)
device     → 3C   ctrl    (challenge)
Controller → 32   device  (controller's system key, encrypted with TRANSFER_KEY)
device     → 3C   ctrl    (second challenge for authentication)
Controller → 3D   device  (answer to second challenge)
```

**Our ESP uses Path 2** (CMD 31 → 3C → 32) which pushes OUR key TO TaHoma.  
**TaHoma expects Path 1** — it is in key-send mode, waiting for CMD 38 so it can send ITS key.

TaHoma correctly responds to CMD 31 with CMD 3C (per spec), but then goes silent on CMD 32 because it is not in key-receive mode and has no use for a foreign system key being pushed to it.

### Correct flow for ESP to receive TaHoma's system key (CMD 38 path)

```
ESP    → 28   (broadcast discover)
TaHoma → 29   (I'm here — type=1023 identifies TaHoma as REMOTE_CONTROLLER)
ESP    → 38   (launch key transfer, 6-byte random challenge)   ← NOT CMD 31
TaHoma → 32   (TaHoma's system key encrypted with TRANSFER_KEY, IV from CMD 38 + challenge)
ESP    → 3C   (challenge TaHoma to prove it knows the key)
TaHoma → 3D   (HMAC response, proves key is genuine)
ESP stores TaHoma's key
```

**CMD 38 is not yet implemented** in `iohome_commands.cpp` / `IoHomeControl.cpp`. The TRANSFER_KEY (`34C3466ED88F4E8E16AA473949884373`) is already defined in `iohome_constants.h` and used in `crypt_2w_key`. Decrypting TaHoma's CMD 32 payload uses the same AES-OFB-like operation: `system_key = CMD32_payload XOR AES(TRANSFER_KEY, IV)` where IV is derived from the CMD 38 frame bytes + the challenge sent in CMD 38.

**How to distinguish TaHoma from a normal screen after CMD 29:** TaHoma's CMD 29 data starts `FF C0` → `type = (0xFFC0 >> 6) & 1023 = 1023`. Normal screens have type ≤ 10. Use device_type > 100 (or == 1023) to select Path 1 instead of Path 2 in `DiscoverAndPairDevice`.

---

## Successful Pairing Reference — May 25 2026 (TaHoma BB0007 → Device CDB322)

```
15:12:42  28  BB0007 → 00003B  (0 bytes)
15:12:42  29  CDB322 → BB0007  0080CDB322029D02F7  [ROLLER_SHUTTER, correct encoding: 00 80]
15:12:44  2C  BB0007 → CDB322  (0 bytes)          ← first 2C (~1300ms after 29)
15:12:44  2C  BB0007 → CDB322  (0 bytes)          ← second 2C (686ms later, TaHoma retransmits)
15:12:44  2D  CDB322 → BB0007  (0 bytes)
15:12:45  28  BB0007 → 00003B  (0 bytes)          [TaHoma continues scanning]
15:12:46  28  BB0007 → 00003B  (0 bytes)
15:12:47  28  BB0007 → 00003B  (0 bytes)
15:12:49  31  BB0007 → CDB322  (0 bytes)          ← first 31 (~5s after 2D)
15:12:50  31  BB0007 → CDB322  (0 bytes)          ← second 31 (retransmit)
15:12:50  3C  CDB322 → BB0007  E82C48E66EEB
15:12:50  32  BB0007 → CDB322  4A31097B0429FF04AE22D717EEBC34E3
15:12:50  36  BB0007 → CDB322  (0 bytes)
15:12:50  37  CDB322 → BB0007  CDB322
```
