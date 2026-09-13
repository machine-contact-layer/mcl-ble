<p align="center">
  <img src="https://raw.githubusercontent.com/machine-contact-layer/.github/main/profile/banner.png" alt="OJOBIT" width="100%">
</p>

<h1 align="center">MCL-BLE</h1>

<p align="center"><strong>MCL over Bluetooth Low Energy, with the roles derived from the wire instead of chosen.</strong></p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-ble/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/machine-contact-layer/mcl-ble/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/machine-contact-layer/mcl-ble/blob/main/LICENSE"><img alt="License Apache-2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue"></a>
  <img alt="profile" src="https://img.shields.io/badge/BLE--GATT%20profile%201-Stable-brightgreen">
  <img alt="activation" src="https://img.shields.io/badge/BLE--ACTIVATE--1-Candidate-yellow">
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-sdk"><b>Use the SDK instead</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core"><b>Specifications</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-link"><b>mcl-link</b></a>
</p>

---

> ### Most people should start with the SDK, not here
>
> This repository is a **specification**. If you are building a product, start
> with [**mcl-sdk**](https://github.com/machine-contact-layer/mcl-sdk): its quickstart runs two machines making
> contact, and the release ships a self-contained developer SDK — one CMake
> project, no sibling checkout. Come back here when you need to know exactly
> what a byte means, or when you are writing an independent implementation.

## Why this exists

BLE is almost everywhere, and it is a natural place for a contact to continue
after two machines have found each other some other way. The hard part is not
the transport — it is that both peers must agree who advertises and who scans
without a human deciding. Here that role is **derived from the wire**, so two
strangers converge on a connection instead of both waiting.

Verified over real radios in both derived role orientations.

**BLE-GATT profile 1 is Stable.** `BLE-ACTIVATE-1`, the activation procedure,
is a Candidate profile.

BLE may be used as an initial MCL transport or as a richer transport negotiated after first contact over another binding.

## Scope

- MCL Link frame carriage over BLE
- connectionless and connected mapping research
- service/characteristic identifiers if needed
- MTU/fragmentation behavior
- preservation of MCL session/context state
- conformance vectors

## Important distinction

MCL-BLE is a binding, not the definition of MCL. MCL Core semantics remain transport-independent.


## Implementation status

The reference implementation is present, freestanding C99, with no allocation
and no global mutable state. It contains **no Bluetooth stack**: how bytes reach the
medium is the integrator's decision. A binding describes a mapping; it does not
become a Bluetooth stack.

Verified: builds under `/W4 /WX`, and the compiled object references no libc
symbol (no `memcpy`, `memset`, `malloc`, or stdio), so it links on a
freestanding target.

- `include/mcl/ble_binding.h` — public API
- `src/ble_binding.c` — implementation
- `tests/test_ble_binding.c` — round trips and the negative cases

## Status

**Not one status. Two, and they are deliberately different.**

| | Status |
|---|---|
| `transport_id = 3` (`MCL_BLE`) | **Stable.** MCL Standards Action, 2026-09-04. Frozen; the record is in [`mcl-link/registries/transport-ids-v0.1.json`](../mcl-link/registries/transport-ids-v0.1.json). |
| [`spec/ble-gatt-profile-v1.md`](spec/ble-gatt-profile-v1.md) — `profile_id = 1` | **Stable.** MCL Standards Action, 2026-09-04. Connected GATT carriage with the fragmentation and reassembly rules fixed by the document. |
| [`spec/binding-v0.md`](spec/binding-v0.md) | **Research Draft.** The wider binding — connectionless advertising carriage in particular — is not frozen and is not a basis for an implementation. |
| `profile_id = 192` | **Experimental Use, permanently.** It was never relabelled: profile 1 is a separate assignment. Evidence gathered under 192 stays evidence about 192. |

So: the GATT profile is safe to build against and the rest of this repository is
not. If you need a single sentence — **what v1.0 freezes here is one profile
under one transport identifier, and nothing else.**

A transport that usually offers link encryption is still not an authenticated
peer. `transport_id = 3` says nothing about security (Architecture Charter
2.11), and this binding performs no pairing, no bonding and no key exchange.

The reference implementation is C99, freestanding, and contains **no Bluetooth
stack**, so nothing here opens a connection for you.

### Evidence

**`E4 MULTI_DEVICE_OVER_AIR`** — 2026-09-02. Windows laptop and an ESP32-S3
peripheral exchanged MCL Link frames over Bluetooth LE, connectionless and
connected. 35 checks, 0 failed. Full record and limits in
[`evidence/e4-ble-gatt-20260902/`](evidence/e4-ble-gatt-20260902/).

Fragmentation was performed at MTU 23, the smallest BLE permits, rather than the
large MTU Windows negotiates — which would have carried every test frame in one
PDU and left the fragmentation path untested.

The four reassembly refusals are the result that matters: a continuation with no
START, a sequence gap, a START whose sequence is not zero, and that START's
orphaned continuation were each **discarded rather than spliced**. A fifth case
carried a frame with a corrupted frame check in well-formed fragments;
reassembly correctly succeeded and Link refused it one layer up. Recovery was
then asserted, because a receiver that silently wedged would otherwise look
identical to one that refused properly.

**Not** independent interoperability. Both ends run the same sources — the host
harness calls them through a shared library rather than reimplementing framing.

Reproduce with [`hardware/esp32-gatt-peer/`](hardware/esp32-gatt-peer/).

### Security

The run used Bluetooth "Just Works" pairing: encrypted against a passive
listener, **unauthenticated against an active one**. A BLE connection
establishes nothing about the peer, and a resolvable private address rotates by
design and is never an identifier. See
[`SECURITY.md`](https://github.com/machine-contact-layer/mcl-core/blob/main/SECURITY.md).
