# MCL-BLE over Bluetooth LE — 2026-09-02

First execution of the MCL-BLE binding between two machines over a real BLE
radio, connectionless and connected.

## Claim

**Conformance:** C1 (canonical encoding), C2 (negative decoding), and the
fragmentation and reassembly rules of C5, demonstrated across a physical link.

**Evidence level:** `E4 MULTI_DEVICE_OVER_AIR`.

**Explicitly not claimed:** C4 or `E6 INDEPENDENT_INTEROPERABILITY`. Both ends
run the same `mcl-ble`, `mcl-link` and `mcl-wire` sources — the firmware
compiles them directly, and the host harness calls them through
`mcl_ble_host.dll` rather than reimplementing framing in C#. That keeps the
result about the binding rather than about the harness, but it also means a
shared misreading of the specification would pass on both sides and be
invisible here.

**Also not claimed:** anything about security. The link was paired with
`DevicePairingProtectionLevel.None`, which is Bluetooth "Just Works": encrypted
against a passive listener, unauthenticated against an active one. Nothing in
this run establishes peer identity, and the binding does not pretend otherwise.

## Setup

| | |
|---|---|
| Peer A (host) | Windows laptop, MediaTek Bluetooth adapter, WinRT GATT client |
| Peer B (board) | DFR1154 / ESP32-S3, BLE peripheral `MCL-BLE-TEST` |
| Service | `6d636c00-0001-4d43-4c00-6d636c626c65` |
| Fragmentation MTU | 23, the smallest BLE permits — 19 payload bytes per PDU |
| Firmware | `mcl-ble/hardware/esp32-gatt-peer`, app partition only at `0x20000` |

Fragmentation is performed at the minimum MTU rather than the negotiated one.
Windows negotiates a large MTU, which would carry every test frame in a single
PDU and leave the fragmentation path — the reason this binding exists —
completely untested.

## Result

```text
35 checks, 0 failed
sustained exchange: acked=20 of 20
board: fragrx=54 ok=24 rej=1 reasmerr=4 fragtx=49 logdrop=0
```

Host and board agree. 24 accepted frames is 3 valid cases plus 1 recovery frame
plus 20 sustained. `logdrop=0` means the serial record is complete rather than
merely quiet.

## Connectionless presence

A 19-byte Tier-0 PRESENCE Link frame was carried in the manufacturer data of the
board's scan response and decoded by the host as a `CONTACT` frame with
`source_ref=0B1E1154`. This is the path by which a machine announces itself to
peers it has never met and cannot yet connect to. `mcl_ble_fits_advertisement`
was asked whether the frame fit rather than being assumed to.

## Fragmentation refusals

The reassembly rules are the reason this binding exists, and a happy-path
exchange never touches them. Four malformed fragment sequences were delivered
over the air, and the board refused all four (`reasmerr=4`) rather than
splicing:

| Case | Outcome |
|---|---|
| continuation fragment with no preceding START | refused, no frame delivered |
| sequence gap between fragments | refused, no frame delivered |
| START whose sequence is not zero | refused, and its continuation then orphaned |
| well-formed fragments carrying a frame with a corrupted CRC | reassembled, then refused by Link, answered with NACK |

The fourth case is the interesting one: reassembly correctly succeeded and the
refusal happened one layer up, which is where it belongs.

Recovery was then asserted rather than assumed. After the four refusals the
board still accepted the next good frame and answered with an ACK, which
distinguishes a reassembler that discarded correctly from one that wedged. A
receiver that had silently stopped answering would otherwise look identical to
one that refused properly.

## Two host-side failures worth recording

Neither was a defect in the binding.

**GATT discovery after pairing.** The harness paired the device and then kept
using the `BluetoothLEDevice` object obtained before pairing. Windows reports
this as a bare `COMException` with no status, which reads like a peripheral
fault. The board's serial log showed `CONNECTED` twice, which is what proved the
radio link was fine and the fault was in the harness. The device is now
re-acquired after pairing.

**A build that reported failure while succeeding.** Windows PowerShell turns any
stderr output from a native command into a terminating error while
`ErrorActionPreference` is `Stop`, so a compiler deprecation warning aborted a
build whose exit code was zero. Both firmware build scripts now treat the exit
code as the authority.

## Reproducing

See [`../../hardware/esp32-gatt-peer/README.md`](../../hardware/esp32-gatt-peer/README.md).

## Files

| File | Contents |
|---|---|
| `host-output.txt` | host peer output, verbatim |
| `board-serial.log` | board's per-fragment and per-frame record, and final counters |
| `SHA256SUMS.txt` | digests of both |
