# MCL BLE-GATT profile v1

Status: **Stable**
Transport: `MCL_BLE`, `transport_id = 3`
Profile identifier: **`profile_id = 1`, `BLE-GATT`** — MCL Standards Action, assigned 2026-09-04. See §10.
Satisfies: `mcl-core/governance/V1_SCOPE.md` §5.6, release gate item 8

## 0. What this document is

The **normative** BLE connected-GATT carriage profile. Every parameter is fixed
by this document rather than by `mcl-ble/src/ble_binding.c`, because a profile
whose parameters are defined by a reference implementation cannot be
implemented independently — and independent implementation is what v1.0 must
demonstrate.

It was published at Candidate first and is now **Stable**, with the profile
identifier assigned. §10 records the completed sequence and the evidence at each
step.

## 1. Scope

```text
IN SCOPE
    the GATT service and characteristics
    direction: which characteristic carries which way
    fragmentation and reassembly, exactly
    the MTU contract, and the minimum every peer must survive
    malformed-fragment behaviour
    rendezvous advertising
    the frame check requirement

NOT IN SCOPE
    connectionless advertising carriage   -- separate profile, not v1
    pairing, bonding, LE Secure Connections -- see section 11
    which BLE address to use, and privacy -- deployment concern
```

## 2. The service

```text
MCL service UUID   6d636c00-0001-4d43-4c00-6d636c626c65
```

A **128-bit** UUID, not a 16-bit one: 16-bit UUIDs are a Bluetooth SIG
allocation, and MCL holds none. Using an unallocated 16-bit value would collide
with whatever the SIG later assigns to it. The bytes spell `mcl` and `mclble` in
the fixed fields so an advertisement is recognisable in a raw capture without a
decoder.

In advertising data the UUID appears in **little-endian** order, as Bluetooth
requires.

### 2.1 Characteristics

| Characteristic | Direction | Operation |
|---|---|---|
| **MCL-RX** | central → peripheral | Write / Write Without Response |
| **MCL-TX** | peripheral → central | Notify |

**Two characteristics, one per direction, and never one shared.** A single
bidirectional characteristic cannot distinguish an echo from a reply, and on a
medium where a peer may hear its own writes reflected, that ambiguity is
indistinguishable from a protocol error.

The GATT roles (central/peripheral) are **carriage roles only**. They carry no
MCL meaning: they are not initiator/responder, they confer no authority, and
`mcl_contact_role_t` is unrelated to them.

## 3. One frame per write, fragmented when necessary

> **Each MCL Link frame is carried by one or more ATT operations, and an ATT
> operation carries bytes from exactly one frame.**

Frames are never concatenated into one PDU, and one PDU never spans two frames.

### 3.1 Fragment header

Every ATT payload begins with **one** header byte:

```text
 bit  7  6  5  4  3  2  1  0
     +--+--+--+--+--+--+--+--+
     |ST|EN|      SEQ       |
     +--+--+--+--+--+--+--+--+

 ST   START  set on the first fragment of a frame
 EN   END    set on the last fragment of a frame
 SEQ  0..63  fragment sequence, wrapping modulo 64
```

A frame fitting a single PDU sets **START and END together**.

### 3.2 The sequence exists to detect loss, not to order

A receiver **MUST** refuse a fragment whose sequence is not the expected
successor. Splicing unrelated bytes into a frame is the failure that turns one
dropped packet into a corrupt semantic object, and reassembly cannot detect it
afterwards.

> **The six-bit sequence MUST NOT wrap within a single frame.**

If it could, a receiver missing exactly one modulus of fragments would see the
sequence it expected and splice. This is not checkable at runtime — by the time
the bytes are wrong they look right — so the whole scheme's safety rests on the
inequality:

```text
ceil(MCL_LINK_FRAME_MAX_SIZE / MIN_PAYLOAD_PER_PDU)  <=  64

  1048 bytes / 19 bytes  =  56 fragments  <=  64      OK
```

The reference implementation asserts this **at compile time**, so raising the
frame limit or lowering the assumed MTU cannot break it silently. An independent
implementation MUST make the same check by some means; discovering the violation
in the field is discovering it too late.

## 4. MTU contract

```text
usable payload per PDU  =  ATT_MTU - 3 (ATT header) - 1 (fragment header)
minimum ATT MTU         =  23  ->  19 bytes of frame per PDU
```

- **A sender MUST NOT assume any MTU above 23.** 23 is the only value every BLE
  peer must support. A larger MTU may be negotiated and used, but a sender that
  requires one cannot talk to a conformant minimal peer.
- **A receiver MUST accept fragments sized for the minimum MTU** even when a
  larger one was negotiated. A peer is permitted to be conservative.
- **A receiver MUST support reassembling `MCL_LINK_FRAME_MAX_SIZE` (1048
  bytes).** A reassembly buffer smaller than the largest legal Link frame makes
  the binding unable to carry a legal frame, and the failure appears only on
  large payloads — the worst way to discover it.
- Fragmentation is exercised **at the 23-byte minimum**, not at a comfortable
  negotiated MTU, because the minimum is the only case every implementation
  must survive.

## 5. Reassembly rules

| Condition | Behaviour |
|---|---|
| Fragment with START while a frame is already in progress | **discard the partial frame**, begin the new one |
| Fragment without START and no frame in progress | discard the fragment |
| Sequence not the expected successor | discard the partial frame |
| Reassembled length exceeds the maximum | discard, never grow the buffer |
| Fragment with zero payload bytes | discard |
| START and END on one fragment | complete frame, deliver |

A discard is **silent and local**. No reply is generated: on an open medium a
rule that answered malformed input with a frame would turn any transmitter in
range into a source of replies from every MCL node that hears it.

**Why a new START abandons the partial frame rather than being refused.** The
sender has evidently restarted, and the most likely cause is that its previous
attempt was lost. Preferring the older partial frame would keep a frame that can
never complete and reject the one that might.

## 6. Frame check

> A sender **MUST** set `MCL_LINK_FLAG_FRAME_CHECK` on every frame carried under
> this profile. A receiver **MUST** verify it after reassembly and refuse a
> frame that fails.

**The reason here is different from the IP profile's, and stronger.** BLE's link
layer already has a 24-bit CRC, and connected GATT is acknowledged and
retransmitted, so radio corruption reaching this layer is unlikely.

**The risk is not the radio — it is the reassembly.** A frame is split across up
to 56 PDUs, each individually protected by the link-layer CRC and each
individually correct. A lost, duplicated, reordered or mis-spliced *fragment*
produces a corrupt frame out of perfectly valid PDUs, and no amount of
link-layer integrity can see it. The fragment sequence catches most of it; the
frame check is what catches the rest.

The check is verified **after** reassembly, over the complete frame — checking
per fragment would verify what the link layer already verified and miss the only
failure mode that matters.

It is a CRC and detects accidental corruption only. It is **not** integrity in
the security sense: anyone who can write to the medium can recompute it.

## 7. Rendezvous advertising

```text
AD structure:  length(1) | type 0x21 | UUID(16, LE) | beacon(8)   = 26 bytes
```

Type `0x21` is Service Data — 128-bit UUID. The 8-byte beacon carries the
`endpoint_token` that a `TRANSPORT_OFFER` named.

26 bytes of the 31 available in a legacy advertising payload, leaving 5 for
flags and anything else the deployment needs. An implementation that cannot fit
its other advertising data alongside must shorten *its own*, not the beacon.

**An advertisement is not an identity.** Hearing one establishes that something
in range transmitted it. A beacon can be recorded and replayed, and this profile
provides nothing that would detect that.

Advertising is **optional**. A peer whose endpoint arrived through a
`TRANSPORT_OFFER` needs no advertisement, and an implementation that never
advertises is fully conformant.

## 8. Error behaviour, exhaustively

| Input | Result |
|---|---|
| Complete frame reassembled, check passes | accepted |
| Frame check absent | refused (§6) |
| Frame check present and wrong | refused |
| Fragment sequence gap | partial frame discarded |
| START mid-frame | partial discarded, new frame begun |
| Non-START fragment with no frame in progress | discarded |
| Reassembly would exceed 1048 bytes | discarded |
| Empty fragment payload | discarded |
| Reassembled bytes are not a valid Link frame | refused |
| Unknown or reserved Link frame class | refused |
| Unsupported Link major | refused |
| Frame addressed to another contact | not delivered to this contact |

Every row is a silent local drop. No row generates a reply.

## 9. What the GATT roles do not mean

Stated because it is the most likely misreading of this profile:

```text
GATT central      != MCL initiator
GATT peripheral   != MCL responder
connected         != authenticated
bonded            != authorized
in range          != trusted
```

A BLE connection is a carriage. `reception != identity != authenticity !=
authority != trust != obligation` holds here exactly as everywhere else in MCL.

## 10. The assigned identifier, and how it was assigned

> **`profile_id = 1` names this profile under `transport_id = 3`.**

A `BLE` `TRANSPORT_OFFER` or `TRANSPORT_ACCEPT` carrying `profile_id = 1` offers
or accepts the carriage defined in this document, and nothing else. Profile
identifiers are transport-scoped: profile 1 under BLE and profile 1 under IP are
unrelated assignments and MUST NOT be compared. That is not a pedantic
restatement — before the registries existed, every binding's examples used
`profile_id = 1` with no definition anywhere, which is exactly the collision
this scoping rule prevents.

The five-step sequence in `GOVERNANCE.md` §4.3 is complete:

```text
1. This document, normative and complete, at Candidate            DONE
2. Independent implementation written against THIS document,
   interoperating using the Experimental Use profile value 192    DONE  C5, 60 checks
3. That interoperability satisfies the registry promotion gate    DONE
4. Standards Action assignment of the final Stable profile value  DONE  profile 1, 2026-09-04
5. C4/C5 re-run with the final assigned bytes                     DONE
```

Step 5 was not ceremony: `profile_id` travels inside `TRANSPORT_OFFER` and
`TRANSPORT_ACCEPT`, so assigning it changed the bytes under test. The C5 run
supporting this profile is the one carrying `profile_id = 1`.

**Profile 192 was not relabelled.** It remains Experimental Use permanently.
Promotion assigned a *new* value in the Standards Action range rather than
changing the status of an old one.

### What step 2's independence was, and was not

The implementation that interoperated in step 2 shares no code, no language and
no build system with the reference C, and it found three real
specification-reading defects. That is evidence this text can be implemented
**from the text alone**.

It was written by the same author, so it is *not* evidence that two
organisations have interoperated, and this assignment does not claim that.
`mcl-core/conformance/ICS.md` states the same boundary.

## 11. What this profile does not provide

- **No confidentiality.** MCL adds none. LE Secure Connections may be used
  beneath this profile; that is the deployment's choice and MCL neither
  requires nor verifies it.
- **No peer authentication.** Bonding authenticates a *BLE address*, which is
  not an MCL identity. A bonded peer is a peer whose radio you have met before.
- **No replay protection.** A recorded advertisement or write replayed later is
  a valid one.
- **No privacy guarantee.** The service UUID is in the clear in advertising
  data, so an MCL peer that advertises is detectable as an MCL peer.

`V1_SCOPE.md` §4.4 defers cryptography conspicuously rather than quietly.

## 12. Conformance

An implementation conforms to BLE-GATT v1 when it:

1. exposes the service and two directional characteristics of §2;
2. fragments and reassembles exactly per §3 and §5, including the
   no-wrap-within-a-frame property;
3. assumes no MTU above 23 when sending, and reassembles up to 1048 bytes;
4. sets and verifies the frame check per §6, after reassembly;
5. refuses every row of §8 without replying;
6. treats GATT roles and bonding per §9 and §11;
7. requires no advertising.

Conformance is **not** established by passing the reference implementation's
unit tests. It is established by interoperating with an implementation that does
not share this one's code.
