# MCL-BLE

`mcl-ble` defines an optional Bluetooth Low Energy binding for the Machine Contact Layer.

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

**Status: Research Draft.** Nothing here is frozen. Assigned transport id
`0x03` is provisional until Candidate Specification maturity.

## Status

Private research repository. Pre-v0.1. See [`spec/binding-v0.md`](spec/binding-v0.md).

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
