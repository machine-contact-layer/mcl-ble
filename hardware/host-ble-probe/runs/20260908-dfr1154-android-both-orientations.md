# BLE-ACTIVATE-1 and BLE-GATT-1, DFR1154 against Android, both orientations

**2026-09-08. Two independent implementations, two physical machines, both
role orientations, raw scan records captured on both sides.**

Peers: DFR1154 (ESP32-S3, Arduino core 3.3.11, NimBLE) and an iQOO 9 on
Android 14 running the MCL Android bench over JNI to the same canonical C.
Neither peer shares BLE code with the other: the board uses the Arduino BLE
wrapper, the phone uses the Android framework API.

adb and the board's SoftAP are control planes only. Every MCL byte reported
here crossed the radio.

## Orientation B — board advertises (offerer), phone scans (acceptor)

The phone matched 46 ms after the scan started, at rssi −18:

    BLE scan MATCH addr=E8:F6:0A:86:7B:D9 rssi=-18
    raw=1921656C626C636D004C434D0100006C636D00000000A5C30F17...

    19                                  length 25
    21                                  Service Data - 128-bit UUID
    656C626C636D004C434D0100006C636D    UUID, little-endian
    00000000A5C30F17                    beacon

The UUID reversed is `6d636c00-0001-4d43-4c00-6d636c626c65`, and the beacon is
four zero bytes followed by the token big-endian. That is BLE-ACTIVATE-1 §3
exactly. The phone then connected as central and opened the candidate.

## Orientation A — phone advertises (offerer), board scans (acceptor)

The board's own scan record, read back over `/api/scan-record`:

    len=29 matches=3 uuid_only=0
    raw=0201021921656C626C636D004C434D0100006C636D00000000A5C30F17

    020102                              Flags, LE General Discoverable
    19 21 <uuid> <beacon>               as above

The leading Flags structure is Android's, added by the platform; the Service
Data structure is byte-identical to the board's own. `uuid_only=0` matters:
every advertisement that carried the MCL UUID also carried the wanted beacon,
so nothing was matched on protocol alone.

Roles were not configured on either side. Only TRANSPORT_OFFER carries an
`endpoint_token`, so the offerer is the peer that can be found: it advertises
and serves, and the acceptor scans and connects. Both orientations were run
without changing that rule, only by changing which machine held the token.

The token itself was supplied by the operator in these runs, and both peers say
so — the phone logs `endpoint_token=A5C30F17 provenance=CONFIGURED (this run is
NOT zero-prior)`. **These runs are not zero-prior and are not offered as
evidence of stranger contact.** They establish activation and carriage.

## BLE-GATT-1 carriage, both directions

A 39-byte frame written from the phone to the board's MCL-RX, echoed back on
MCL-TX:

    BLE frame in 39 bytes: 1001000102030405060708090A0B0C0D0E0F
                           101112131415161718191A1B1C1D1E1F2021222324

Byte-identical, and repeatedly — both ends echo, so the frame ping-pongs, which
is a property of the diagnostic and not of the protocol. At the 23-byte minimum
MTU a 39-byte frame is three fragments, so this exercises fragmentation and
reassembly in both directions.

## A defect this found, and the instrument that found it

The board **panicked at the end of every orientation-B run**: `reset_reason=4`
(ESP_RST_PANIC) at 48 015 ms of a run that began at 3 015 ms and was configured
for 45 000 — exactly `scenario_end` — in phase ACTIVATE with 11 648 bytes free.

`ble_stack_down()` disconnected a *client* before `BLEDevice::deinit(true)` but
never disconnected a *server's* peer, so when this node was the offerer the
stack was torn out from under a live GATT link.

The panic destroys the RAM log ring, so the board came back idle with an empty
log — indistinguishable from a run that finished. It was visible only because
the run footprint is mirrored into RTC memory each loop and read back one boot
later. **The scan and connection results above are unaffected**: they were
captured on the phone and in the board's scan record before teardown.

Fixed by stopping advertising, disconnecting either role's peer, and waiting
for the disconnect to complete before deinit. Firmware
`BAF628BC2C38672AF60590285A42537AA52E4839C70B7AABF49283E119A33283` — **built
but not yet flashed or verified**; flashing this board needs the USB port,
which the phone was occupying.

## Also fixed here: an Android scan that silently returns nothing

The first orientation-B attempt found nothing while the board advertised for
fifty seconds. The manifest declared `BLUETOOTH_SCAN` without
`neverForLocation`, which on Android 12 and later makes scan results contingent
on `ACCESS_FINE_LOCATION` — capped at API 30 in this manifest, so never held on
Android 14. No error and no denial: the callback is simply never invoked, and
a working advertiser is indistinguishable from an absent one. The flag is now
claimed, and it is accurate: this scanner matches on advertisement contents and
derives nothing about location.

## Audit correction, 2026-09-09

The original account above is retained, but its both-orientation activation
and carriage claim is too strong for the records it contains. Orientation A
records a scanner match; scenario 5 does not call the connection function. The
39-byte ping-pong and Android-central connection establish the other
orientation. Do not cite this report as proof of DFR-central connection.

The full adapter also reconstructed NimBLE native address bytes with the
reversing byte-array constructor. An on-board negative control now demonstrates
that `c1:23:45:67:89:ab` becomes `ab:89:67:45:23:c1` under that old path, while
the corrected `ble_addr_t` path preserves bytes and address type. This is an
adapter defect, not a reason to change the BLE role or token rules. New
central-carriage evidence must come from an actual connection and exact echo,
not another scan.
