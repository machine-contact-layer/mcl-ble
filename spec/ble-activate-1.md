# BLE-ACTIVATE-1

**Status:** **Candidate.** Normatively complete and implementable from this
document alone. Not Stable: see §7.

**Layer:** MCL-BLE, transport 3
**Profile identifier:** none, and none is requested. See §1.2.
**Modifies:** **nothing.** `BLE-GATT-1` is Stable and is not touched by this
document, which adds no service, no characteristic, no UUID, no wire field and
no `profile_id`.

---

## 1. The gap this closes

### 1.1 What the rendezvous leaves undone

`AP-BOOTSTRAP-1` gets two strangers as far as an agreed candidate bearer. If
that bearer is BLE, both machines then hold this:

```text
offerer   knows its own endpoint_token, which it published
acceptor  knows the offerer's endpoint_token, from TRANSPORT_OFFER
```

and nothing else. `TRANSPORT_ACCEPT` is 16 bytes at Wire major 1 and carries no
`endpoint_token`, so the asymmetry is not an oversight that can be corrected
with a field — the size is fixed for the major.

`BLE-GATT-1` does not close it either, and correctly so: it specifies carriage
over an *established* GATT connection. §7 of that profile makes advertising
**optional** and an implementation that never advertises fully conformant,
which is right for a profile about carriage and leaves first contact undefined.

So two independent builders, both fully conformant, can each wait for the other
to connect. The guaranteed intersection is empty. That is the same finding as
`mcl-core/research/TWO_BUILDER_AUDIT.md`, one layer down.

### 1.2 Why this is a separate document and not an edit

`BLE-GATT-1` is Stable. Stable means its normative content does not change, and
adding a "you must advertise" rule to it would change what a conformant
implementation has to do — for every deployment, including the many that never
touch acoustic rendezvous and have their addresses by other means.

This profile therefore constrains only a **deployment that names BLE as a
candidate bearer under `AP-BOOTSTRAP-1`**. Everything it says is about the
window between `TRANSPORT_ACCEPT` and the first Link frame on the candidate. It
needs no `profile_id`, because it names no new carriage: the connection it
brings up carries `BLE-GATT-1`, `profile_id = 1`, unchanged.

## 2. Roles are derived from the wire, not chosen

The obvious way to write this section is to declare which peer advertises. That
would be an arbitrary choice, and an arbitrary choice is what two builders
cannot converge on.

There is no need to choose. Only `TRANSPORT_OFFER` carries an
`endpoint_token`, and it is the offerer's own. So:

| Peer | What it knows | What it must therefore do |
|---|---|---|
| Offerer | its own token; no address for the acceptor | **advertise**; GATT **peripheral** |
| Acceptor | the offerer's token | **scan and connect**; GATT **central** |

Any other assignment requires information the wire does not carry. An
implementation claiming this profile **MUST** use this one.

Under `AP-BOOTSTRAP-1` §8.1 the offerer is the round's *responder* and becomes
the migration controller, so the peripheral is also the peer that sends
`PATH_CHALLENGE`. That is not a contradiction: a GATT central initiates the
connection, and either side writes once it is up.

**GATT roles still mean nothing else.** `BLE-GATT-1` §9 holds without
modification: central is not initiator, connected is not authenticated, bonded
is not authorized, in range is not trusted.

## 3. Finding the peer

The advertisement is the one defined in `BLE-GATT-1` §7, used rather than
extended:

```text
AD structure:  length(1) | type 0x21 | UUID(16, LE) | beacon(8)   = 26 bytes
```

The 8-byte beacon **MUST** be the 32-bit `endpoint_token` from the
`TRANSPORT_OFFER`, zero-extended and big-endian:

```text
beacon = 00 00 00 00 | endpoint_token[31:24] | [23:16] | [15:8] | [7:0]
```

The scanning peer **MUST** match on the service UUID **and** the full 8-byte
beacon, and **MUST NOT** connect to an advertiser that matches only the UUID.
The UUID identifies the protocol; the beacon identifies the transaction.

**`endpoint_token` MUST be non-zero** for a BLE candidate under this profile.
Zero is legal at the Wire layer and means "reach me by the profile's own
discovery" — and this profile's discovery *is* the token, so zero names
nothing. An implementation **MUST NOT** offer BLE as a candidate bearer with a
zero token while claiming this profile.

**A token is not a name and an advertisement is not an identity.** It is a
transaction selector, valid for one activation window. It can be observed,
recorded and replayed by anything in range, and neither this profile nor
`BLE-GATT-1` provides anything that would detect that. A matching beacon
establishes that something in range transmitted those bytes. Nothing more.

## 4. The activation window is derived

Both durations come from the rendezvous constants rather than from preference.

The acceptor cannot answer a `PATH_CHALLENGE` it has not received, and the
controller is permitted `MCL_RDV_MAX_HANDOFF_RETRIES` retransmissions of a lost
one, each a full response timeout apart. So the window must cover the
controller's whole schedule:

```text
(4 retries + 1) x 6000 ms response timeout  =  30 000 ms
```

- The offerer **MUST** advertise continuously for at least 30 s after emitting
  its offer, or until a matching central connects.
- The acceptor **MUST** scan for at least 30 s after emitting its acceptance,
  or until it connects.
- Both **MUST** stop when the migration completes or the contact is abandoned.

An implementation that advertises for less is not merely impatient: it can go
quiet while a correctly behaving peer is still retrying, and the failure looks
like an unsupported bearer.

**Advertising duty cycle is a platform matter and is deliberately unspecified.**
Operating systems limit advertising, and a rule this profile cannot enforce is
not worth stating. The 30 s figure bounds the *window*, not the interval within
it.

## 5. What happens on connection

1. The central connects and discovers the `BLE-GATT-1` service.
2. Either peer **MAY** negotiate a larger ATT MTU. Neither **MAY** assume one:
   `BLE-GATT-1` §4 stands.
3. The controller sends `PATH_CHALLENGE` as an ordinary Link frame over the
   characteristics of `BLE-GATT-1` §2. Nothing in the handoff changes.
4. If the connection drops before the migration completes, both peers **MUST**
   resume §4's behaviour for the remainder of the window. The handoff controls
   are idempotent by construction, so a repeated control after a reconnection
   is safe and is the intended recovery.

## 6. Conformance

An implementation conforms to BLE-ACTIVATE-1 when it:

1. conforms to `BLE-GATT-1`, unmodified;
2. assigns advertiser and scanner roles by §2 and by nothing else;
3. encodes and matches the beacon exactly per §3, on UUID **and** beacon;
4. refuses to offer BLE with a zero `endpoint_token`;
5. sustains the §4 window on both sides;
6. changes no part of the handoff sequence.

Conformance is **not** established by passing this project's tests.

## 7. Why this is Candidate and not Stable

**The complete builder-interoperability criterion remains open.** The retained
DFR1154/Android campaign establishes exact-token discovery in both orientations
and GATT carriage with Android as central and the board as peripheral. Its
reverse-orientation scan record does not establish connection or carriage.
These platform stacks are different implementations, but this is not outside
organisational review or a complete zero-prior machine lifecycle. Full
lifecycle qualification, contention and public review remain required.

There is a second, narrower reason. §3 spends four of the beacon's eight bytes
on zeroes. That is the honest encoding of a 32-bit token into a field
`BLE-GATT-1` already fixed at eight bytes, and it is defensible — but if a
later revision wants a wider token or a transaction discriminator in those
bytes, it will want them from a Stable profile that has already promised they
are zero. Leaving this Candidate keeps that decision open until somebody
outside this project has an opinion about it.

Until this profile or an equivalent is Stable, **`MCL Stranger-Contact 1` over
a BLE candidate bearer is not guaranteed between two builders who never
coordinate.** That is stated here rather than left for an integrator to
discover, and it is tracked in `mcl-core/governance/RELEASE_GATE_V1.md` row 35.

## 8. What this profile does not provide

- No authentication, no confidentiality, no integrity beyond the frame check
  that `BLE-GATT-1` §6 already requires. A frame check is not integrity.
- No protection against a replayed beacon.
- No guarantee that the advertiser is the machine that made the offer.
- No identity. `reception != identity != authenticity != authority != trust
  != obligation` holds here as everywhere else in MCL.
