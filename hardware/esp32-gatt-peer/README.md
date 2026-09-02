# MCL-BLE over-air peer (ESP32-S3)

A second machine to talk to. The MCL-BLE binding is otherwise only ever
exercised against its own reassembler in the same process, which is the one
condition under which a fragmentation scheme cannot fail.

The board advertises a Tier-0 PRESENCE Link frame in its scan response and
exposes a GATT service that accepts fragmented Link frames and returns
fragmented replies by notification. The host harness in
[`tools/ble-over-air-peer`](../../tools/ble-over-air-peer) drives it.

## What this measures, and what it does not

It measures whether MCL Link frames survive BLE fragmentation and reassembly
between two machines, and — the part that matters — whether the receiver
**discards rather than splices** when fragments are lost, reordered, orphaned or
non-canonical.

It does **not** measure security. The connection uses Bluetooth "Just Works"
pairing: encrypted against a passive listener, unauthenticated against an active
one. Nothing here establishes peer identity, and a BLE connection must never be
read as evidence of one.

It does **not** demonstrate independent interoperability. Both ends run the same
sources, so a shared misreading of the specification would pass on both sides.

## Fragmentation is done at the minimum MTU on purpose

Windows negotiates a large ATT MTU, which would carry every test frame in a
single PDU and leave the fragmentation path completely untested. Both ends
therefore fragment at 23, the smallest MTU BLE permits, giving 19 payload bytes
per PDU. That is the case the scheme has to survive, so it is the case the
experiment uses.

## The host harness runs the shipped C code

Windows exposes BLE scanning and GATT only through WinRT, so the harness is
written in C#. Reimplementing framing and fragmentation there would mean the
over-air result described the harness rather than the binding.

Instead `mcl-ble` is compiled into `mcl_ble_host.dll` together with `mcl-link`
and `mcl-wire`, and the harness calls into it through
[`tools/ble_host_shim.c`](../../tools/ble_host_shim.c), which adds no protocol
logic of its own. Even the fragment header bit layout is read from the C rather
than restated in C#, so the two definitions cannot drift.

## Running it

```powershell
# 1. Build. Compiles only; never uploads.
.\build-firmware.ps1

# 2. Write the application partition, and nothing else.
.\flash-app-only.ps1 -PortName COM3

# 3. Build the shim and the harness.
cmake -S ..\..  -B <build> -DMCL_BLE_BUILD_TOOLS=ON
dotnet build ..\..\tools\ble-over-air-peer -c Release -o <out>
#    then place mcl_ble_host.dll beside the harness executable

# 4. Run. The board must be advertising.
mcl_ble_over_air_peer.exe --count 20
```

The first run pairs the board, which Windows requires before it will open a
GATT session. A device object obtained before pairing keeps failing afterwards,
so the harness re-acquires it.

## Flashing safety

Only the application partition at `0x20000` is written. The bootloader at `0x0`
and the partition table at `0x8000` are untouched, which is what makes the
operation reversible from an application-only backup.

`flash-app-only.ps1` refuses to run unless a factory application backup exists,
so a board cannot be put into a state it cannot be returned from. Uploading
through the Arduino toolchain is deliberately not used: its upload step can also
rewrite the bootloader and the partition table.

## Serial interface

| Command | Response |
|---|---|
| `PING` | `MCLPONG` |
| `STATUS` | connection state and the fragment/frame counters |
| `RESET` | zeroes the counters |

`STATUS` reports `logdrop`, the number of log lines dropped because the USB CDC
transmit buffer was full. Logging is dropped rather than blocked: a blocked
`Serial.printf` stalls the BLE callback it was called from, and the run would
then measure the logger instead of the link. A non-zero `logdrop` means the
serial record is incomplete, which must never be mistaken for a quiet one.
