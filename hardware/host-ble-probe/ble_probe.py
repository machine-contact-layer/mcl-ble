#!/usr/bin/env python3
"""
Host-side BLE probe for BLE-ACTIVATE-1 and BLE-GATT-1.

LAB INSTRUMENT. This is not an MCL implementation and no conformance claim
rests on it. It shares no code with the reference stack, which makes it useful
for one narrow purpose: checking that what a machine actually puts on the air
matches what the profile says, rather than checking that the reference agrees
with itself.

WHAT IT CHECKS

  scan     the advertisement carries Service Data for the MCL 128-bit service
           UUID, and the service data is EXACTLY the 8-byte beacon of
           BLE-ACTIVATE-1 section 3: four zero bytes, then the 32-bit
           endpoint_token big-endian.

  connect  the peer exposes the MCL service with an MCL-RX characteristic that
           accepts writes and an MCL-TX characteristic that notifies, and a
           frame written in BLE-GATT-1 fragments comes back reassembled.

WHAT IT DOES NOT ESTABLISH

  Independent interoperability. It was written by the same author as the stack
  it probes, from the same documents. A shared misreading of the specification
  is invisible to it, exactly as it is to the two ends of a single-vendor
  hardware run.

Requires `bleak`. Windows, Linux and macOS all expose enough for the scan; the
raw advertising payload is available on some platforms only, so the check is
written against parsed service data, which is what every platform gives.
"""

import argparse
import asyncio
import sys

from bleak import BleakClient, BleakScanner

MCL_SERVICE_UUID = "6d636c00-0001-4d43-4c00-6d636c626c65"
MCL_RX_CHAR_UUID = "6d636c00-0002-4d43-4c00-6d636c626c65"
MCL_TX_CHAR_UUID = "6d636c00-0003-4d43-4c00-6d636c626c65"

BEACON_SIZE = 8
ATT_DEFAULT_MTU = 23
ATT_HEADER = 3
FRAG_HEADER = 1
MAX_PAYLOAD_PER_PDU = ATT_DEFAULT_MTU - ATT_HEADER - FRAG_HEADER  # 19

FRAG_START = 0x80
FRAG_END = 0x40
FRAG_SEQ_MASK = 0x3F


def beacon_for(token: int) -> bytes:
    """BLE-ACTIVATE-1 section 3: zero-extended, big-endian."""
    return b"\x00\x00\x00\x00" + token.to_bytes(4, "big")


def fragment(frame: bytes, mtu: int = ATT_DEFAULT_MTU):
    """BLE-GATT-1 section 3.1. One header byte: START | END | 6-bit sequence."""
    per = mtu - ATT_HEADER - FRAG_HEADER
    if per <= 0:
        raise ValueError("MTU too small to carry a fragment")
    out = []
    total = max(1, (len(frame) + per - 1) // per)
    for i in range(total):
        chunk = frame[i * per : (i + 1) * per]
        header = (i & FRAG_SEQ_MASK)
        if i == 0:
            header |= FRAG_START
        if i == total - 1:
            header |= FRAG_END
        out.append(bytes([header]) + chunk)
    return out


class Reassembler:
    """The receiving half, written from the profile rather than from the C."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.buffer = b""
        self.expect_seq = None

    def push(self, pdu: bytes):
        if not pdu:
            return None
        header = pdu[0]
        seq = header & FRAG_SEQ_MASK
        start = bool(header & FRAG_START)
        end = bool(header & FRAG_END)
        if start:
            self.buffer = b""
            self.expect_seq = seq
        elif self.expect_seq is None or seq != (self.expect_seq + 1) % 64:
            # "A receiver MUST refuse a fragment whose sequence is not the
            # expected successor." Splicing is the failure reassembly cannot
            # detect afterwards.
            self.reset()
            raise ValueError(f"fragment out of sequence: {seq}")
        else:
            self.expect_seq = seq
        self.buffer += pdu[1:]
        if end:
            frame = self.buffer
            self.reset()
            return frame
        return None


async def cmd_scan(args):
    want = beacon_for(args.token) if args.token is not None else None
    print(f"scanning {args.seconds}s for service {MCL_SERVICE_UUID}")
    if want is not None:
        print(f"  wanted beacon {want.hex().upper()}  (token {args.token:#010x})")

    seen = {}

    def callback(device, adv):
        data = adv.service_data.get(MCL_SERVICE_UUID)
        if data is None:
            return
        key = device.address
        if key in seen:
            return
        seen[key] = (device, adv, data)

        ok_len = len(data) == BEACON_SIZE
        ok_pad = data[:4] == b"\x00\x00\x00\x00" if ok_len else False
        token = int.from_bytes(data[4:8], "big") if ok_len else None
        matched = (want is None) or (data == want)

        print(f"\n  {device.address}  rssi={adv.rssi}")
        print(f"    service data      {data.hex().upper()}  ({len(data)} bytes)")
        print(f"    length is 8       {'yes' if ok_len else 'NO'}")
        print(f"    upper 4 bytes 0   {'yes' if ok_pad else 'NO'}")
        if token is not None:
            print(f"    endpoint_token    {token:#010x}")
            if token == 0:
                print("    REFUSED: a zero token names nothing under "
                      "BLE-ACTIVATE-1 section 3")
        if want is not None:
            print(f"    matches wanted    {'YES' if matched else 'no (UUID only)'}")

    # NO service_uuids FILTER, DELIBERATELY.
    #
    # The BLE-ACTIVATE-1 advertisement carries ONE structure: Service Data for
    # the 128-bit MCL UUID. It does not carry a "complete list of 128-bit
    # service UUIDs" section, because the profile does not define one and a
    # legacy advertising PDU has 31 bytes to spend. A platform filter that
    # matches on advertised service UUIDs therefore does not match it -- and on
    # Windows/WinRT that filter silently returns nothing at all, which looks
    # exactly like a dead advertiser.
    #
    # So the scan is unfiltered and the match is made here, which is what an
    # implementation on a platform without service-data filtering has to do.
    scanner = BleakScanner(detection_callback=callback)
    await scanner.start()
    await asyncio.sleep(args.seconds)
    await scanner.stop()

    if not seen:
        print("\nnothing advertising the MCL service was heard")
        return 1
    if want is not None and not any(d == want for (_, _, d) in seen.values()):
        print("\nthe MCL service was seen but no advertiser carried the wanted beacon")
        return 1
    print(f"\n{len(seen)} MCL advertiser(s)")
    return 0


async def cmd_connect(args):
    if args.token == 0:
        print("REFUSED: BLE-ACTIVATE-1 forbids a zero endpoint token")
        return 1
    want = beacon_for(args.token) if args.token is not None else None
    print(f"scanning {args.seconds}s for a connectable MCL advertiser")

    target = None

    def match(device, adv):
        data = adv.service_data.get(MCL_SERVICE_UUID)
        if data is None:
            return False
        # "MUST match on the service UUID AND the full 8-byte beacon, and MUST
        # NOT connect to an advertiser that matches only the UUID."
        return True if want is None else data == want

    target = await BleakScanner.find_device_by_filter(match, timeout=args.seconds)
    if target is None:
        print("no advertiser matched")
        return 1
    print(f"found {target.address}")

    reasm = Reassembler()
    received = []
    rejected = []

    def on_notify(_char, data: bytearray):
        try:
            frame = reasm.push(bytes(data))
        except ValueError as exc:
            rejected.append(str(exc))
            print(f"  notify REFUSED: {exc}")
            return
        if frame is not None:
            received.append(frame)
            print(f"  reassembled {len(frame)} bytes: {frame.hex().upper()}")

    async with BleakClient(target) as client:
        print(f"connected: {client.is_connected}")
        services = client.services
        svc = services.get_service(MCL_SERVICE_UUID)
        if svc is None:
            print("MCL service absent")
            return 1
        rx = svc.get_characteristic(MCL_RX_CHAR_UUID)
        tx = svc.get_characteristic(MCL_TX_CHAR_UUID)
        print(f"  MCL-RX {'present' if rx else 'ABSENT'} "
              f"properties={rx.properties if rx else '-'}")
        print(f"  MCL-TX {'present' if tx else 'ABSENT'} "
              f"properties={tx.properties if tx else '-'}")
        if rx is None or tx is None:
            return 1
        if "notify" not in tx.properties:
            print("MCL-TX does not notify")
            return 1
        if not ({"write", "write-without-response"} & set(rx.properties)):
            print("MCL-RX accepts no writes")
            return 1

        await client.start_notify(tx, on_notify)

        payload = bytes(args.bytes_to_send)
        frags = fragment(payload)
        print(f"writing {len(payload)} bytes as {len(frags)} fragment(s) "
              f"at the minimum MTU")
        for f in frags:
            await client.write_gatt_char(
                rx, f, response="write-without-response" not in rx.properties)
            await asyncio.sleep(0.02)

        await asyncio.sleep(args.wait)
        await client.stop_notify(tx)

    print(f"frames received back: {len(received)}")
    if rejected or received != [payload]:
        print("FAIL: expected exactly one byte-identical echo and no rejected fragments")
        return 1
    print("PASS: exact byte-for-byte round trip")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p_scan = sub.add_parser("scan", help="verify an advertiser's AD structure")
    p_scan.add_argument("--token", type=lambda s: int(s, 0), default=None,
                        help="endpoint_token the beacon must carry")
    p_scan.add_argument("--seconds", type=float, default=10.0)

    p_conn = sub.add_parser("connect", help="connect and exchange one frame")
    p_conn.add_argument("--token", type=lambda s: int(s, 0), default=None)
    p_conn.add_argument("--seconds", type=float, default=10.0)
    p_conn.add_argument("--wait", type=float, default=3.0,
                        help="seconds to wait for notifications after writing")
    p_conn.add_argument("--bytes-to-send", type=lambda s: bytes.fromhex(s),
                        default=bytes(range(1, 41)),
                        help="hex payload to write (default: 40 counting bytes, "
                             "which needs three fragments)")

    args = parser.parse_args()
    handler = {"scan": cmd_scan, "connect": cmd_connect}[args.command]
    return asyncio.run(handler(args))


if __name__ == "__main__":
    sys.exit(main())
