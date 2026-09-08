"""Probe verdict regressions; simulated transport, no hardware evidence."""
import argparse
import contextlib
import io
import unittest
import sys
import types
from unittest.mock import patch

# The verdict test substitutes the radio completely and needs no Bluetooth
# package or device. The real instrument still requires Bleak at runtime.
with patch.dict(sys.modules, {"bleak": types.SimpleNamespace(BleakClient=None, BleakScanner=None)}):
    import ble_probe as probe


class ProbeVerdictTests(unittest.IsolatedAsyncioTestCase):
    async def run_case(self, echo, properties=None):
        args = argparse.Namespace(token=0xA5C30F17, seconds=0, wait=0,
                                  bytes_to_send=bytes(range(1, 41)))
        writes = []

        class Scanner:
            @staticmethod
            async def find_device_by_filter(match, timeout):
                device = argparse.Namespace(address="test-peer")
                adv = argparse.Namespace(service_data={
                    probe.MCL_SERVICE_UUID: probe.beacon_for(args.token)})
                if not match(device, adv):
                    raise AssertionError("exact beacon did not match")
                adv.service_data[probe.MCL_SERVICE_UUID] += b"\x00"
                if match(device, adv):
                    raise AssertionError("overlong beacon matched")
                return device

        class Client:
            def __init__(self, target):
                self.is_connected = True
                self.services = self
                self.callback = None

            async def __aenter__(self):
                return self

            async def __aexit__(self, *exc):
                pass

            def get_service(self, uuid):
                return self

            def get_characteristic(self, uuid):
                return argparse.Namespace(properties=(properties or
                    ["write-without-response"]) if uuid == probe.MCL_RX_CHAR_UUID
                    else ["notify"])

            async def start_notify(self, tx, callback):
                self.callback = callback

            async def write_gatt_char(self, rx, fragment, response):
                writes.append((bytes(fragment), response))
                if len(writes) == 3:
                    for pdu in echo:
                        self.callback(None, bytearray(pdu))

            async def stop_notify(self, tx):
                pass

        with patch.object(probe, "BleakScanner", Scanner), \
             patch.object(probe, "BleakClient", Client), \
             contextlib.redirect_stdout(io.StringIO()):
            result = await probe.cmd_connect(args)
        self.assertEqual([p for p, _ in writes], probe.fragment(args.bytes_to_send))
        return result, writes

    async def test_exact_echo_passes(self):
        result, _ = await self.run_case(probe.fragment(bytes(range(1, 41))))
        self.assertEqual(result, 0)

    async def test_silent_peer_fails(self):
        result, _ = await self.run_case([])
        self.assertEqual(result, 1)

    async def test_corrupted_echo_fails(self):
        result, _ = await self.run_case(probe.fragment(b"x" * 40))
        self.assertEqual(result, 1)

    async def test_duplicate_echo_fails(self):
        result, _ = await self.run_case(probe.fragment(bytes(range(1, 41))) * 2)
        self.assertEqual(result, 1)

    async def test_rejected_fragment_followed_by_exact_echo_fails(self):
        result, _ = await self.run_case([b"\x01x"] +
                                      probe.fragment(bytes(range(1, 41))))
        self.assertEqual(result, 1)

    async def test_write_only_characteristic_uses_response(self):
        result, writes = await self.run_case(probe.fragment(bytes(range(1, 41))),
                                             ["write"])
        self.assertEqual(result, 0)
        self.assertTrue(all(response for _, response in writes))


if __name__ == "__main__":
    unittest.main()
