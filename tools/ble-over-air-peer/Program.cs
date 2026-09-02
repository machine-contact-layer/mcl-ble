// MCL-BLE over-air host peer.
//
// Drives the ESP32-S3 peripheral in mcl-ble/hardware/esp32-gatt-peer over a real
// Bluetooth LE link, in two modes:
//
//   1. Connectionless. Scans for the board's advertisement and decodes the
//      Tier-0 PRESENCE Link frame carried in its manufacturer data.
//   2. Connected. Writes fragmented Link frames to the RX characteristic and
//      reassembles the fragmented replies delivered by notification.
//
// This program supplies the radio and nothing else. Every frame it builds, every
// fragment it produces, every reassembly and every decode is performed by
// mcl_ble_host.dll, which is compiled from the same mcl-ble, mcl-link and
// mcl-wire sources the firmware runs. A harness that reimplemented framing in C#
// would be testing itself against the board rather than testing the binding.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Enumeration;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

internal static class Mcl
{
    private const string Dll = "mcl_ble_host";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_link_frame_max_size();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_ble_default_mtu();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_ble_payload_per_pdu(int attMtu);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_ble_fits_advertisement(int frameSize);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_build_frame(int frameClass, int flags, uint sourceRef,
                                                int sequence, int kind,
                                                byte[] outBuf, int outCapacity);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_fragment_count(int frameSize, int attMtu);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_fragment(byte[] frame, int frameSize, int attMtu,
                                             int index, byte[] outBuf, int outCapacity);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_reassembler_size();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mclx_reassembler_reset(IntPtr state);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_reassemble(IntPtr state, byte[] fragment, int fragmentSize,
                                               byte[] outBuf, int outCapacity);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_decode_frame(byte[] frame, int frameSize,
                                                 out int frameClass, out uint sourceRef,
                                                 out int sequence, out int kind);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_frag_start_bit();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_frag_end_bit();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int mclx_frag_seq_mask();

    // Link frame classes, mirroring spec/link-v0.md section 4.
    internal const int ClassContact = 0;
    internal const int ClassData = 3;
    internal const int ClassAck = 4;
    internal const int ClassNack = 5;
    internal const int ClassKeepalive = 6;

    internal const int FlagSequence = 0x04;
    internal const int FlagFrameCheck = 0x10;

    internal const int KindPresence = 0;
    internal const int KindHazard = 1;
    internal const int KindNone = -1;
}

internal static class Program
{
    private const string ServiceUuid = "6d636c00-0001-4d43-4c00-6d636c626c65";
    private const string RxCharUuid = "6d636c00-0002-4d43-4c00-6d636c626c65";
    private const string TxCharUuid = "6d636c00-0003-4d43-4c00-6d636c626c65";
    private const string DeviceName = "MCL-BLE-TEST";
    private const uint HostSourceRef = 0xC0FFEE02u;

    private static int _checks;
    private static int _failed;

    private static void Check(bool condition, string message)
    {
        _checks++;
        if (!condition)
        {
            _failed++;
            Console.WriteLine("    FAIL: " + message);
        }
    }

    private static async Task<int> Main(string[] args)
    {
        int count = 20;
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "--count" && i + 1 < args.Length) { count = int.Parse(args[++i]); }
        }

        Console.WriteLine("MCL-BLE over-air peer");
        Console.WriteLine("=====================");
        Console.WriteLine($"frame_max={Mcl.mclx_link_frame_max_size()} " +
                          $"default_mtu={Mcl.mclx_ble_default_mtu()} " +
                          $"payload_per_pdu={Mcl.mclx_ble_payload_per_pdu(Mcl.mclx_ble_default_mtu())}");
        Console.WriteLine();

        ulong address = await ScanAsync();
        if (address == 0)
        {
            Console.WriteLine("Board advertisement not seen. Is the peripheral firmware running?");
            return 1;
        }

        await ConnectedAsync(address, count);

        Console.WriteLine();
        Console.WriteLine($"{_checks} checks, {_failed} failed");
        return _failed == 0 ? 0 : 1;
    }

    // ---------- Connectionless ----------

    private static async Task<ulong> ScanAsync()
    {
        Console.WriteLine("  [+] connectionless presence in advertising data");

        var watcher = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
        var completion = new TaskCompletionSource<ulong>();
        byte[] captured = null;

        watcher.Received += (_, e) =>
        {
            // Manufacturer data under the reserved 0xFFFF test identifier.
            var section = e.Advertisement.ManufacturerData.FirstOrDefault(m => m.CompanyId == 0xFFFF);
            if (section == null) { return; }

            var reader = DataReader.FromBuffer(section.Data);
            var payload = new byte[section.Data.Length];
            reader.ReadBytes(payload);

            captured ??= payload;
            completion.TrySetResult(e.BluetoothAddress);
        };

        watcher.Start();
        var winner = await Task.WhenAny(completion.Task, Task.Delay(TimeSpan.FromSeconds(20)));
        watcher.Stop();

        if (winner != completion.Task || captured == null)
        {
            Check(false, "advertisement carrying an MCL frame was seen");
            return 0;
        }

        Console.WriteLine($"      advertised frame: {captured.Length} bytes");
        Check(Mcl.mclx_ble_fits_advertisement(captured.Length) == 1,
              "advertised frame fits BLE advertising data");

        int status = Mcl.mclx_decode_frame(captured, captured.Length,
                                           out int cls, out uint src, out int seq, out int kind);
        Check(status == 0, $"advertised bytes decode as a Link frame (status {status})");
        Check(cls == Mcl.ClassContact, "advertised frame is a CONTACT frame");
        Check(kind == Mcl.KindPresence, "advertised frame carries PRESENCE");
        Console.WriteLine($"      decoded: class={cls} src={src:X8} seq={seq} kind={kind}");

        return await completion.Task;
    }

    // ---------- Connected ----------

    private sealed class Reassembler : IDisposable
    {
        private readonly IntPtr _state;
        public Reassembler()
        {
            _state = Marshal.AllocHGlobal(Mcl.mclx_reassembler_size());
            Mcl.mclx_reassembler_reset(_state);
        }
        public void Reset() => Mcl.mclx_reassembler_reset(_state);
        public int Feed(byte[] fragment, byte[] outBuf) =>
            Mcl.mclx_reassemble(_state, fragment, fragment.Length, outBuf, outBuf.Length);
        public void Dispose() => Marshal.FreeHGlobal(_state);
    }

    private static async Task ConnectedAsync(ulong address, int count)
    {
        var device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
        if (device == null) { Check(false, "connected to the board"); return; }

        // Windows will not open a GATT session to an unpaired peripheral. Pair
        // when the device is not already known, then re-acquire it: the object
        // obtained before pairing refers to the unpaired device and its GATT
        // calls keep failing after the bond exists.
        if (!device.DeviceInformation.Pairing.IsPaired &&
            device.DeviceInformation.Pairing.CanPair)
        {
            var custom = device.DeviceInformation.Pairing.Custom;
            custom.PairingRequested += (_, request) => request.Accept();
            var pairing = await custom.PairAsync(DevicePairingKinds.ConfirmOnly,
                                                 DevicePairingProtectionLevel.None);
            Console.WriteLine($"      pairing: {pairing.Status}");

            device.Dispose();
            await Task.Delay(1500);
            device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
            if (device == null) { Check(false, "re-acquired the board after pairing"); return; }
        }

        GattDeviceServicesResult services = null;
        for (int attempt = 0; attempt < 6; attempt++)
        {
            try
            {
                // Enumerating every service, rather than filtering by UUID,
                // is what reliably drives Windows to establish the connection.
                services = await device.GetGattServicesAsync(BluetoothCacheMode.Uncached);
                if (services.Status == GattCommunicationStatus.Success &&
                    services.Services.Any(x => x.Uuid == new Guid(ServiceUuid)))
                {
                    break;
                }
                Console.WriteLine($"      discovery attempt {attempt + 1}: status={services.Status} " +
                                  $"services={services.Services.Count}");
            }
            catch (Exception ex)
            {
                // Report the reason rather than swallowing it: a peripheral
                // that has just started advertising is often not connectable
                // for the first attempt or two, and that looks the same as a
                // genuine failure unless the HRESULT is printed.
                Console.WriteLine($"      discovery attempt {attempt + 1}: " +
                                  $"0x{ex.HResult:X8} {ex.Message.Trim()}");
            }
            await Task.Delay(2000);
        }

        var service = services?.Services.FirstOrDefault(x => x.Uuid == new Guid(ServiceUuid));
        if (service == null)
        {
            Check(false, $"MCL service discovered (status {services?.Status.ToString() ?? "no response"})");
            device.Dispose();
            return;
        }
        Check(true, "MCL service discovered");

        var rx = (await service.GetCharacteristicsForUuidAsync(new Guid(RxCharUuid))).Characteristics.FirstOrDefault();
        var tx = (await service.GetCharacteristicsForUuidAsync(new Guid(TxCharUuid))).Characteristics.FirstOrDefault();
        Check(rx != null, "RX characteristic found");
        Check(tx != null, "TX characteristic found");
        if (rx == null || tx == null) { return; }

        int frameMax = Mcl.mclx_link_frame_max_size();
        var assembled = new byte[frameMax];
        using var reassembler = new Reassembler();
        var frames = new List<byte[]>();
        var gate = new SemaphoreSlim(0);
        int fragmentsReceived = 0;
        int reassemblyRefusals = 0;

        tx.ValueChanged += (_, e) =>
        {
            var fragment = new byte[e.CharacteristicValue.Length];
            DataReader.FromBuffer(e.CharacteristicValue).ReadBytes(fragment);
            fragmentsReceived++;

            int result = reassembler.Feed(fragment, assembled);
            if (result > 0)
            {
                frames.Add(assembled.Take(result).ToArray());
                gate.Release();
            }
            else if (result < 0)
            {
                reassemblyRefusals++;
            }
            // result == 0 means more fragments are expected, which is normal.
        };

        var subscribe = await tx.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.Notify);
        Check(subscribe == GattCommunicationStatus.Success, "subscribed to notifications");
        if (subscribe != GattCommunicationStatus.Success) { return; }

        // Fragment at the smallest MTU BLE permits rather than at whatever this
        // connection negotiated. Windows negotiates a large MTU, which would let
        // every frame travel in one PDU and leave the fragmentation path
        // untested. The minimum is the case the scheme has to survive.
        int mtu = Mcl.mclx_ble_default_mtu();

        await ExchangeCase(rx, gate, frames, "valid CONTACT carrying PRESENCE",
                           Mcl.ClassContact, Mcl.FlagSequence | Mcl.FlagFrameCheck, 1,
                           Mcl.KindPresence, mtu, frameMax, Mcl.ClassAck);

        await ExchangeCase(rx, gate, frames, "valid DATA carrying HAZARD",
                           Mcl.ClassData, Mcl.FlagSequence | Mcl.FlagFrameCheck, 2,
                           Mcl.KindHazard, mtu, frameMax, Mcl.ClassAck);

        await ExchangeCase(rx, gate, frames, "KEEPALIVE with no payload",
                           Mcl.ClassKeepalive, 0, 0, Mcl.KindNone, mtu, frameMax, Mcl.ClassAck);

        await FragmentationNegativeCases(rx, gate, frames, mtu, frameMax);

        await SustainedCase(rx, gate, frames, count, mtu, frameMax);

        Console.WriteLine($"      fragments received={fragmentsReceived} reassembly refusals={reassemblyRefusals}");
        Check(reassemblyRefusals == 0, "no fragment was refused by reassembly");

        service.Dispose();
        device.Dispose();
    }

    private static byte[] BuildFrame(int cls, int flags, int seq, int kind, int frameMax)
    {
        var buf = new byte[frameMax];
        int n = Mcl.mclx_build_frame(cls, flags, HostSourceRef, seq, kind, buf, buf.Length);
        if (n <= 0) { throw new InvalidOperationException($"frame build refused, status {-n}"); }
        return buf.Take(n).ToArray();
    }

    private static async Task<bool> SendFragmented(GattCharacteristic rx, byte[] frame, int mtu)
    {
        int count = Mcl.mclx_fragment_count(frame.Length, mtu);
        if (count <= 0) { return false; }

        for (int i = 0; i < count; i++)
        {
            var pdu = new byte[mtu];
            int n = Mcl.mclx_fragment(frame, frame.Length, mtu, i, pdu, pdu.Length);
            if (n <= 0) { return false; }

            var writer = new DataWriter();
            writer.WriteBytes(pdu.Take(n).ToArray());
            var status = await rx.WriteValueAsync(writer.DetachBuffer(),
                                                  GattWriteOption.WriteWithoutResponse);
            if (status != GattCommunicationStatus.Success) { return false; }
            await Task.Delay(12);
        }
        return true;
    }

    private static async Task<byte[]> AwaitReply(SemaphoreSlim gate, List<byte[]> frames, int timeoutMs)
    {
        if (!await gate.WaitAsync(timeoutMs)) { return null; }
        lock (frames)
        {
            var frame = frames[^1];
            frames.Clear();
            return frame;
        }
    }

    private static async Task ExchangeCase(GattCharacteristic rx, SemaphoreSlim gate,
                                           List<byte[]> frames, string label,
                                           int cls, int flags, int seq, int kind,
                                           int mtu, int frameMax, int expectClass)
    {
        Console.WriteLine($"  [+] {label}");
        var frame = BuildFrame(cls, flags, seq, kind, frameMax);
        int fragments = Mcl.mclx_fragment_count(frame.Length, mtu);
        Console.WriteLine($"      frame={frame.Length} bytes in {fragments} fragments at MTU {mtu}");

        Check(await SendFragmented(rx, frame, mtu), "frame sent as fragments");

        var reply = await AwaitReply(gate, frames, 6000);
        Check(reply != null, "peer replied with a reassembled frame");
        if (reply == null) { return; }

        int status = Mcl.mclx_decode_frame(reply, reply.Length,
                                           out int rcls, out uint rsrc, out int rseq, out int rkind);
        Check(status == 0, $"reply decodes as a Link frame (status {status})");
        Check(rcls == expectClass, $"reply class is {expectClass}, got {rcls}");
    }

    private static async Task<bool> SendRaw(GattCharacteristic rx, byte[] pdu)
    {
        var writer = new DataWriter();
        writer.WriteBytes(pdu);
        var status = await rx.WriteValueAsync(writer.DetachBuffer(),
                                              GattWriteOption.WriteWithoutResponse);
        await Task.Delay(12);
        return status == GattCommunicationStatus.Success;
    }

    private static byte[] Fragment(byte[] frame, int mtu, int index)
    {
        var pdu = new byte[mtu];
        int n = Mcl.mclx_fragment(frame, frame.Length, mtu, index, pdu, pdu.Length);
        if (n <= 0) { throw new InvalidOperationException($"fragment refused, status {-n}"); }
        return pdu.Take(n).ToArray();
    }

    /*
     * The reassembly rules are the reason this binding exists, and they are the
     * part a happy-path exchange never touches. Each case below delivers a
     * fragment sequence that must be refused, and then proves the receiver
     * recovered: a reassembler that discarded correctly still accepts the next
     * good frame, while one that spliced or wedged does not. Recovery is
     * asserted rather than assumed, because a receiver that silently stopped
     * answering would otherwise look identical to one that refused properly.
     */
    private static async Task FragmentationNegativeCases(GattCharacteristic rx,
                                                         SemaphoreSlim gate,
                                                         List<byte[]> frames,
                                                         int mtu, int frameMax)
    {
        int startBit = Mcl.mclx_frag_start_bit();
        int seqMask = Mcl.mclx_frag_seq_mask();

        var frame = BuildFrame(Mcl.ClassContact, Mcl.FlagSequence | Mcl.FlagFrameCheck,
                               50, Mcl.KindPresence, frameMax);
        int fragments = Mcl.mclx_fragment_count(frame.Length, mtu);
        if (fragments < 2)
        {
            Check(false, "the negative cases need a frame that spans at least two fragments");
            return;
        }

        // A continuation with no preceding START. There is nothing to join it to.
        Console.WriteLine("  [-] continuation fragment with no START");
        Check(await SendRaw(rx, Fragment(frame, mtu, 1)), "orphan fragment delivered");
        Check(await AwaitReply(gate, frames, 2500) == null,
              "orphan fragment produced no frame");

        // A sequence gap. Joining across it would build one frame from two
        // valid halves of different frames.
        Console.WriteLine("  [-] sequence gap between fragments");
        Check(await SendRaw(rx, Fragment(frame, mtu, 0)), "START delivered");
        var gapped = Fragment(frame, mtu, 1);
        gapped[0] = (byte)((gapped[0] & ~seqMask) | ((gapped[0] + 1) & seqMask));
        Check(await SendRaw(rx, gapped), "fragment with a skipped sequence delivered");
        Check(await AwaitReply(gate, frames, 2500) == null,
              "sequence gap produced no frame");

        // A START must open the sequence at zero to be canonical.
        Console.WriteLine("  [-] START whose sequence is not zero");
        var badStart = Fragment(frame, mtu, 0);
        badStart[0] = (byte)((badStart[0] & ~seqMask) | 1);
        Check(await SendRaw(rx, badStart), "non-canonical START delivered");
        Check(await SendRaw(rx, Fragment(frame, mtu, 1)), "its continuation delivered");
        Check(await AwaitReply(gate, frames, 2500) == null,
              "non-canonical START produced no frame");

        // Fragments are well formed, but the frame inside fails its integrity
        // check. Reassembly must succeed and Link must refuse.
        Console.WriteLine("  [-] valid fragments carrying a frame with a corrupted CRC");
        var corrupt = (byte[])frame.Clone();
        corrupt[^1] ^= 0x01;
        int corruptFragments = Mcl.mclx_fragment_count(corrupt.Length, mtu);
        for (int i = 0; i < corruptFragments; i++)
        {
            await SendRaw(rx, Fragment(corrupt, mtu, i));
        }
        var nack = await AwaitReply(gate, frames, 4000);
        Check(nack != null, "peer replied to a frame that failed integrity");
        if (nack != null)
        {
            Mcl.mclx_decode_frame(nack, nack.Length, out int ncls, out _, out _, out _);
            Check(ncls == Mcl.ClassNack, $"peer refused with NACK, got class {ncls}");
        }

        // Recovery: the receiver must still accept a good frame.
        Console.WriteLine("  [+] a good frame after the refusals");
        Check(await SendFragmented(rx, frame, mtu), "good frame sent");
        var reply = await AwaitReply(gate, frames, 4000);
        Check(reply != null, "receiver recovered and answered");
        if (reply != null)
        {
            Mcl.mclx_decode_frame(reply, reply.Length, out int cls, out _, out _, out _);
            Check(cls == Mcl.ClassAck, "recovery reply is an ACK");
        }
    }

    private static async Task SustainedCase(GattCharacteristic rx, SemaphoreSlim gate,
                                            List<byte[]> frames, int count, int mtu, int frameMax)
    {
        Console.WriteLine($"  [=] sustained exchange of {count} fragmented frames");
        int acked = 0;

        for (int i = 0; i < count; i++)
        {
            var frame = BuildFrame(Mcl.ClassContact, Mcl.FlagSequence | Mcl.FlagFrameCheck,
                                   100 + i, Mcl.KindPresence, frameMax);
            if (!await SendFragmented(rx, frame, mtu)) { continue; }

            var reply = await AwaitReply(gate, frames, 6000);
            if (reply == null) { continue; }
            if (Mcl.mclx_decode_frame(reply, reply.Length, out int cls, out _, out _, out _) == 0 &&
                cls == Mcl.ClassAck)
            {
                acked++;
            }
        }

        Console.WriteLine($"      acked={acked} of {count}");
        Check(acked * 2 > count, "a majority of fragmented frames completed a round trip");
    }
}
