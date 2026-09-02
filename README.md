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
