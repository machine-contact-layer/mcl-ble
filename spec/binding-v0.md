# MCL-BLE Binding v0

Status: **Research Draft**

## Purpose

Carry MCL Link frames over Bluetooth Low Energy while preserving the same MCL semantic/session contract used by other bindings.

## Candidate modes

Research should compare:

- connectionless advertising-based contact
- periodic advertising where appropriate
- PAwR-like request/reply patterns where platform support exists
- connected GATT/L2CAP-style carriage for richer sessions

The binding should not require every platform to support every BLE mode.

## Capability advertisement

A compact BLE advertisement may expose:

- MCL protocol identifier
- Core/Wire version digest
- ephemeral contact identifier
- supported BLE binding modes
- pointer/reference to richer capability data

## Session handoff

If MCL contact begins acoustically and moves to BLE, the BLE binding should preserve:

- MCL session reference
- semantic context generation
- peer claim references
- priority semantics

No BLE connection should be interpreted as proof of identity or authority by itself.

## Fragmentation

MCL Wire objects exceeding the selected BLE payload size require deterministic fragmentation/reassembly or use of a richer connected mode.

## Open questions

- smallest useful connectionless MCL frame on common BLE stacks
- mapping MCL priority to BLE scheduling/retransmission behavior
- coexistence with OS-controlled advertising limits
- relationship between MCL discovery and native BLE discovery
