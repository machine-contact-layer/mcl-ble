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

## Status

Private research repository. Pre-v0.1. See [`spec/binding-v0.md`](spec/binding-v0.md).
