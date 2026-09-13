<p align="center">
  <img src="https://raw.githubusercontent.com/machine-contact-layer/.github/main/profile/banner.png" alt="Machine Contact Layer (MCL) banner: black and white checkerboard with the OJOBIT wordmark" width="100%">
</p>

<h1 align="center">MCL-BLE</h1>

<p align="center"><strong>MCL over Bluetooth Low Energy, with the roles derived from the wire instead of chosen.</strong></p>

<p align="center">
  Bluetooth Low Energy (BLE) transport binding for the Machine Contact Layer
  (MCL): carry machine-to-machine contact frames over GATT with fragmentation and
  reassembly down to MTU 23, and derive advertiser and scanner roles so two
  devices converge on a connection. Freestanding C99.
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-ble/actions/workflows/ci.yml"><img alt="CI status" src="https://github.com/machine-contact-layer/mcl-ble/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/machine-contact-layer/mcl-ble/blob/main/LICENSE"><img alt="License: Apache-2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue"></a>
  <img alt="BLE-GATT profile 1: Stable" src="https://img.shields.io/badge/BLE--GATT%20profile%201-Stable-brightgreen">
  <img alt="BLE-ACTIVATE-1: Candidate" src="https://img.shields.io/badge/BLE--ACTIVATE--1-Candidate-yellow">
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-sdk"><b>SDK</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core"><b>MCL overview</b></a> ·
  <a href="spec/ble-gatt-profile-v1.md"><b>Specification</b></a> ·
  <a href="evidence/"><b>Evidence</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core/blob/main/REPORTING.md"><b>Report a defect</b></a>
</p>

---

BLE is on almost every device, and it is a natural place for a contact to begin
or to continue after two machines have found each other some other way. The
hard part is not moving bytes — it is that two strangers must agree who
advertises and who scans without a human deciding. Here that role is **derived
from the wire**, so they converge on a connection instead of both waiting.

It is part of the [Machine Contact Layer](https://github.com/machine-contact-layer/mcl-core),
an open protocol for machine-to-machine discovery, contact and transport
migration across BLE, IP and acoustic links.

## What MCL-BLE provides

- **BLE-GATT profile 1 (Stable)** — connected GATT carriage of MCL Link frames,
  with fixed fragmentation and reassembly rules that work down to the minimum
  ATT MTU of 23
- **BLE-ACTIVATE-1 (Candidate)** — how two devices that met elsewhere reach a
  BLE connection: the peer that can be found advertises, the peer that knows the
  endpoint token scans
- **Deterministic reassembly refusal** — a continuation with no start, a
  sequence gap or an orphaned fragment is discarded, never spliced
- **A freestanding C99 reference** with no allocation, no global mutable state
  and no Bluetooth stack: you connect it to the one your platform already has

## Use it

The normal path is [mcl-sdk](https://github.com/machine-contact-layer/mcl-sdk):
its reference deployment migrates a contact onto BLE-GATT profile 1, and you
supply the characteristic writes and hand received bytes to the SDK.

Using the binding directly:

- [`include/mcl/ble_binding.h`](include/mcl/ble_binding.h) — public API
- [`src/ble_binding.c`](src/ble_binding.c) — implementation
- [`tests/test_ble_binding.c`](tests/test_ble_binding.c) — round trips and refusal cases

The library builds under `/W4 /WX`, and the compiled object references no libc
symbol, so it links on a freestanding target.

## Maturity

| | Status |
|---|---|
| `transport_id = 3` (`MCL_BLE`) | **Stable.** Recorded in [`mcl-link/registries/transport-ids-v0.1.json`](https://github.com/machine-contact-layer/mcl-link/blob/main/registries/transport-ids-v0.1.json). |
| [`spec/ble-gatt-profile-v1.md`](spec/ble-gatt-profile-v1.md) — `profile_id = 1` | **Stable.** Safe to build against. |
| [`spec/ble-activate-1.md`](spec/ble-activate-1.md) — `BLE-ACTIVATE-1` | **Candidate.** Normatively complete and implementable. |
| [`spec/binding-v0.md`](spec/binding-v0.md) | **Research Draft.** Connectionless advertising carriage is not frozen. |
| `profile_id = 192` | **Experimental Use**, permanently. Profile 1 is a separate assignment. |

## Verified on hardware

- **Laptop ↔ ESP32-S3 over BLE**, connectionless and connected, 35 checks, 0
  failed. Fragmentation ran at MTU 23 rather than the large MTU a desktop
  negotiates, so the fragmentation path was genuinely exercised; the reassembly
  refusal cases were each discarded, and a frame with a corrupted frame check was
  reassembled and then refused one layer up by Link —
  [`evidence/e4-ble-gatt-20260902/`](evidence/e4-ble-gatt-20260902/), rig in
  [`hardware/esp32-gatt-peer/`](hardware/esp32-gatt-peer/).
- **BLE-ACTIVATE-1 role derivation in both orientations**, between an ESP32-S3
  and an Android handset —
  [`hardware/host-ble-probe/runs/`](hardware/host-ble-probe/runs/20260908-dfr1154-android-both-orientations.md).
- **One contact carried across 100 alternating BLE/IP migrations** —
  [`mcl-sdk/evidence/`](https://github.com/machine-contact-layer/mcl-sdk/tree/main/evidence).

## Security

A BLE connection establishes nothing about the peer. This binding performs no
pairing, no bonding and no key exchange, and `transport_id = 3` says nothing
about security. "Just Works" pairing is encrypted against a passive listener
but unauthenticated against an active one, and a resolvable private address
rotates by design and is never an identifier. For authentication, use LE Secure
Connections beneath the profile. See
[`SECURITY.md`](https://github.com/machine-contact-layer/mcl-core/blob/main/SECURITY.md).

## Related repositories

[mcl-core](https://github.com/machine-contact-layer/mcl-core) ·
[mcl-link](https://github.com/machine-contact-layer/mcl-link) ·
[mcl-sdk](https://github.com/machine-contact-layer/mcl-sdk) ·
[mcl-ip](https://github.com/machine-contact-layer/mcl-ip) ·
[mcl-ap](https://github.com/machine-contact-layer/mcl-ap)

## License

Apache-2.0. See [`LICENSE`](LICENSE).
