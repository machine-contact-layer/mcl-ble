# Host BLE probe

**LAB INSTRUMENT.** Not an MCL implementation, and no conformance claim rests
on it.

A machine with a Bluetooth radio, a Python interpreter and `bleak` can check
what another machine actually puts on the air, without sharing a line of code
with it. That is worth having for one narrow reason: a reference implementation
tested against itself agrees with itself. This agrees, or does not, with the
**profile documents**.

```bash
pip install bleak

# does an advertiser encode BLE-ACTIVATE-1 section 3 correctly?
python ble_probe.py scan --token 0xA5C30F17 --seconds 12

# does it carry BLE-GATT-1 frames over the connection that follows?
python ble_probe.py connect --token 0xA5C30F17
```

## What it checks

**`scan`** — that the advertisement carries Service Data for the MCL 128-bit
service UUID, that the service data is exactly eight bytes, and that those
bytes are the `endpoint_token` zero-extended and big-endian. It also refuses a
zero token out loud, because `BLE-ACTIVATE-1` §3 forbids one for a BLE
candidate: this profile's discovery *is* the token, so zero names nothing.

**`connect`** — that the peer exposes the MCL service with an **MCL-RX** that
accepts writes and an **MCL-TX** that notifies, and that a frame written as
`BLE-GATT-1` fragments **at the 23-byte minimum MTU** comes back reassembled.
The default payload is 40 bytes, which needs three fragments, so START, middle
and END are all exercised rather than only the single-PDU case.

The command succeeds only for exactly one byte-identical echo and no rejected
notification fragments. Silence, corruption, duplicate echoes and a rejected
fragment followed by a valid echo all fail. This verdict is covered by
`python -m unittest discover -s . -p test_ble_probe.py`; those simulated tests
do not count as radio evidence.

The reassembler here refuses a fragment whose sequence is not the expected
successor, as §3.2 requires. That rule is what stops one dropped packet turning
into a corrupt semantic object, and a probe that spliced quietly would be a
worse instrument than none.

## The platform trap this found

The first version filtered the scan by service UUID, which is the obvious thing
to write, and it heard **nothing at all** from a board that was advertising
perfectly.

`BLE-ACTIVATE-1` advertises one structure: Service Data for the 128-bit MCL
UUID. It does not advertise a *complete list of 128-bit service UUIDs*, because
the profile defines no such structure and a legacy advertising PDU has 31 bytes
to spend. A platform filter that matches on advertised service **UUIDs** does
not match service **data**, and on Windows/WinRT it silently returns nothing —
which is indistinguishable from a dead advertiser.

So the probe scans unfiltered and matches in software, which is what an
implementation on a platform without service-data filtering has to do anyway.
Android's `ScanFilter.setServiceData()` can do it in the controller; not every
platform can.

## What it does not establish

**Independent interoperability.** It was written by the same author, from the
same documents, as the stack it probes. It shares no source, no language and no
build system with the reference, which makes it good at catching an
implementation that disagrees with its own specification. It cannot catch a
specification that both halves misread the same way — the same limit
`mcl-core/conformance/independent/` states about itself.

## Runs

- [`runs/20260907-dfr1154-activate-gatt.log`](runs/20260907-dfr1154-activate-gatt.log)
  — the DFR1154 autonomous node's advertisement and GATT carriage, including
  the board's own heap at the moment it held the connection.
