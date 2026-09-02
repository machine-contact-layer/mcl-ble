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

## Endpoint offer

Canonical encoding, network byte order, fixed 14 bytes:

```text
u8   role               0 peripheral, 1 central, 2 broadcast
u8   address_type       0 public, 1 random static, 2 random private resolvable
u8   address[6]
u16  att_mtu            negotiated or expected; below 23 is malformed, not small
u16  service_ref        integrator-scoped service/characteristic reference
u16  validity_s         0 means unspecified, not infinite
```

A resolvable private address rotates by design. It is a privacy mechanism, and
MCL MUST NOT treat it as a stable identifier or as evidence of identity.

## Fragmentation

This is the problem BLE poses that IP does not. An ATT payload is 20 bytes at
the default 23-byte MTU and often no more than 244 after negotiation, while
advertising data is 31 bytes. A Link frame carrying a Tier-0 object is around
20 bytes and fits; anything richer does not.

So the binding owns fragmentation explicitly rather than leaving each integrator
to invent an incompatible scheme. Every fragment carries one header byte:

```text
bit 7      START   first fragment of a frame
bit 6      END     last fragment of a frame
bits 0..5  SEQ     fragment sequence, modulo 64
```

A frame that fits one PDU sets START and END together. Usable payload per PDU is
`att_mtu - 3 (ATT) - 1 (fragment header)`, so 19 bytes at the default MTU.

### Reassembly rules

A conforming receiver MUST discard rather than splice:

- a fragment whose sequence is not the one expected, since a gap means a lost or
  reordered fragment and joining across it produces a corrupt semantic object
  from two valid halves;
- a continuation with no preceding START, which is orphaned;
- a START whose sequence is not zero, which is non-canonical;
- any frame that would exceed the reassembly limit.

A START always begins a new frame. Any frame already in progress is dropped
without being delivered: the peer either restarted or its END was missed, and
either outcome forbids joining the old bytes to the new frame.

## Connectionless presence

A Link frame small enough to fit advertising data may be broadcast with no
connection, which is how a machine announces presence to peers it has never met.
A Tier-0 PRESENCE Link frame fits within the 31-byte limit.

## Open questions

- smallest useful connectionless MCL frame on common BLE stacks
- mapping MCL priority to BLE scheduling/retransmission behavior
- coexistence with OS-controlled advertising limits
- relationship between MCL discovery and native BLE discovery
