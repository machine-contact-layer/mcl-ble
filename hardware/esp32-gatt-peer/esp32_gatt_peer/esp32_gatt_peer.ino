/*
 * MCL-BLE over-air peer for the DFR1154 (ESP32-S3).
 *
 * A BLE peripheral that carries MCL Link frames two ways:
 *
 *   1. Connectionless. A Tier-0 PRESENCE Link frame is broadcast in the
 *      manufacturer data of the advertisement, which is how a machine
 *      announces itself to peers it has never met and cannot yet connect to.
 *
 *   2. Connected. A GATT service accepts fragmented Link frames on one
 *      characteristic and returns fragmented replies by notification on
 *      another, using this binding's own fragmentation scheme.
 *
 * The second is the one that matters. Fragmentation is the problem BLE poses
 * that IP does not, and a scheme that has only ever been tested against its own
 * reassembler in the same process has not been tested against the reordering,
 * loss and MTU behaviour of a real stack.
 *
 * The sketch supplies the radio, the GATT table and the serial log. All
 * encoding, fragmentation, reassembly and validation is done by the same
 * mcl-ble, mcl-link and mcl-wire sources the host uses.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

extern "C" {
#include "mcl/ble_binding.h"
#include "mcl/link.h"
#include "mcl/wire.h"
}

/* Integrator-scoped identifiers for this experiment. */
#define MCL_SERVICE_UUID   "6d636c00-0001-4d43-4c00-6d636c626c65"
#define MCL_RX_CHAR_UUID   "6d636c00-0002-4d43-4c00-6d636c626c65"  /* host -> board */
#define MCL_TX_CHAR_UUID   "6d636c00-0003-4d43-4c00-6d636c626c65"  /* board -> host */

/* Contact reference this board puts in the frames it sends. Not an identity. */
static const uint32_t kBoardSourceRef = 0x0B1E1154u;

static BLEServer         *g_server = nullptr;
static BLECharacteristic *g_tx_char = nullptr;
static bool g_connected = false;

static uint32_t g_frag_rx      = 0;   /* fragments received */
static uint32_t g_frames_ok    = 0;   /* frames reassembled and accepted */
static uint32_t g_frames_rej   = 0;   /* frames reassembled but refused */
static uint32_t g_reasm_err    = 0;   /* fragment-level reassembly refusals */
static uint32_t g_frag_tx      = 0;   /* fragments sent */
static uint32_t g_log_dropped  = 0;
static uint16_t g_tx_sequence  = 0;

static mcl_ble_reassembler_t g_reasm;
static uint8_t g_frame_buf[MCL_LINK_FRAME_MAX_SIZE];
static uint8_t g_wire_buf[MCL_WIRE_TIER0_MAX_SIZE];

/*
 * Log only when the USB CDC transmit buffer has room. Serial.printf blocks once
 * that buffer fills, which it does immediately if no host is reading the port,
 * and a blocked log stalls the BLE callback it was called from. An instrument
 * that perturbs what it measures is worse than no instrument, so a line is
 * dropped and counted instead, and STATUS reports the count.
 */
#define MCL_LOG_HEADROOM 192

static bool log_ready(void)
{
    if (Serial.availableForWrite() >= MCL_LOG_HEADROOM) {
        return true;
    }
    g_log_dropped++;
    return false;
}

/* The board's own presence object. */
static void fill_presence(mcl_wire_tier0_t *obj)
{
    memset(obj, 0, sizeof(*obj));
    obj->kind = MCL_WIRE_KIND_PRESENCE;
    obj->priority = 2u;                        /* P2_NORMAL */
    obj->source_ref = kBoardSourceRef;
    obj->body.presence.machine_class = 3u;
    obj->body.presence.capability_tag = 0x17C0DEu;   /* 24-bit field */
    obj->body.presence.ttl = 30u;
}

/* Build a Link frame carrying the board's presence. Returns length, or 0. */
static size_t build_presence_frame(uint8_t *out, size_t cap, uint8_t flags,
                                   mcl_link_frame_class_t cls,
                                   uint32_t destination_ref)
{
    mcl_wire_tier0_t obj;
    mcl_link_frame_t f;
    size_t wire_len = 0, written = 0;

    fill_presence(&obj);
    if (mcl_wire_tier0_encode(&obj, g_wire_buf, sizeof(g_wire_buf), &wire_len) != MCL_WIRE_OK) {
        return 0;
    }

    memset(&f, 0, sizeof(f));
    f.frame_class = cls;
    f.flags = flags;
    f.source_ref = kBoardSourceRef;
    f.destination_ref = destination_ref;
    f.sequence = g_tx_sequence;
    f.payload = g_wire_buf;
    f.payload_len = (uint16_t)wire_len;

    if (mcl_link_frame_encode(&f, out, cap, &written) != MCL_LINK_OK) {
        return 0;
    }
    return written;
}

/*
 * Send a Link frame to the connected peer, fragmented at the smallest MTU BLE
 * permits rather than at whatever this connection negotiated.
 *
 * That is deliberate. Windows negotiates a large MTU, which would let every
 * frame travel in one PDU and leave the fragmentation path untested. The
 * minimum MTU is the case the scheme has to survive, so it is the case the
 * experiment uses.
 */
static void send_frame_fragmented(const uint8_t *frame, size_t frame_size)
{
    const uint16_t mtu = MCL_BLE_ATT_DEFAULT_MTU;
    uint8_t pdu[MCL_BLE_ATT_DEFAULT_MTU];
    size_t count, i, written = 0;

    if (g_tx_char == nullptr || !g_connected) {
        return;
    }

    count = mcl_ble_fragment_count(frame_size, mtu);
    if (count == 0u) {
        if (log_ready()) { Serial.println("MCLBLE TXERR fragment_count"); }
        return;
    }

    for (i = 0u; i < count; ++i) {
        if (mcl_ble_fragment(frame, frame_size, mtu, i, pdu, sizeof(pdu), &written) != MCL_BLE_OK) {
            if (log_ready()) { Serial.println("MCLBLE TXERR fragment"); }
            return;
        }
        g_tx_char->setValue(pdu, written);
        g_tx_char->notify();
        g_frag_tx++;
        delay(8);   /* let the stack drain; a burst is dropped, not queued */
    }
}

/* A complete frame has been reassembled. Validate and answer it. */
static void handle_frame(const uint8_t *frame, size_t frame_size)
{
    mcl_link_frame_t f;
    size_t consumed = 0;
    size_t reply_len;

    if (mcl_link_frame_decode(frame, frame_size, &f, &consumed) != MCL_LINK_OK ||
        consumed != frame_size) {
        g_frames_rej++;
        if (log_ready()) {
            Serial.printf("MCLBLE FRAME n=%u REJECT link\n", (unsigned)frame_size);
        }
        reply_len = build_presence_frame(g_frame_buf, sizeof(g_frame_buf), 0u,
                                         MCL_LINK_CLASS_NACK, 0u);
        if (reply_len > 0) { send_frame_fragmented(g_frame_buf, reply_len); }
        return;
    }

    if (f.frame_class == MCL_LINK_CLASS_CONTACT || f.frame_class == MCL_LINK_CLASS_DATA) {
        mcl_wire_tier0_t obj;
        size_t wire_consumed = 0;
        mcl_wire_status_t wst = mcl_wire_tier0_decode(f.payload, (size_t)f.payload_len,
                                                      &obj, &wire_consumed);
        if (wst != MCL_WIRE_OK || wire_consumed != (size_t)f.payload_len) {
            g_frames_rej++;
            if (log_ready()) {
                Serial.printf("MCLBLE FRAME n=%u cls=%u REJECT wire\n",
                              (unsigned)frame_size, (unsigned)f.frame_class);
            }
            reply_len = build_presence_frame(g_frame_buf, sizeof(g_frame_buf), 0u,
                                             MCL_LINK_CLASS_NACK, f.source_ref);
            if (reply_len > 0) { send_frame_fragmented(g_frame_buf, reply_len); }
            return;
        }
        g_frames_ok++;
        if (log_ready()) {
            Serial.printf("MCLBLE FRAME n=%u cls=%u src=%08lX seq=%u kind=%u ACCEPT\n",
                          (unsigned)frame_size, (unsigned)f.frame_class,
                          (unsigned long)f.source_ref, (unsigned)f.sequence,
                          (unsigned)obj.kind);
        }
    } else {
        g_frames_ok++;
        if (log_ready()) {
            Serial.printf("MCLBLE FRAME n=%u cls=%u src=%08lX seq=%u ACCEPT nosem\n",
                          (unsigned)frame_size, (unsigned)f.frame_class,
                          (unsigned long)f.source_ref, (unsigned)f.sequence);
        }
    }

    reply_len = build_presence_frame(g_frame_buf, sizeof(g_frame_buf),
                                     MCL_LINK_FLAG_DESTINATION | MCL_LINK_FLAG_SEQUENCE |
                                     MCL_LINK_FLAG_FRAME_CHECK,
                                     MCL_LINK_CLASS_ACK, f.source_ref);
    if (reply_len > 0) {
        g_tx_sequence++;
        send_frame_fragmented(g_frame_buf, reply_len);
    }
}

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String v = c->getValue();
        const uint8_t *data = (const uint8_t *)v.c_str();
        size_t len = v.length();
        size_t frame_size = 0;
        mcl_ble_status_t st;

        if (len == 0) { return; }
        g_frag_rx++;

        st = mcl_ble_reassemble(&g_reasm, data, len, &frame_size);
        if (st == MCL_BLE_OK) {
            handle_frame(g_reasm.buffer, frame_size);
        } else if (st == MCL_BLE_ERR_INCOMPLETE) {
            /* Normal: more fragments are expected. Not an error condition. */
        } else {
            g_reasm_err++;
            if (log_ready()) {
                Serial.printf("MCLBLE FRAG n=%u REJECT reasm=%ld\n",
                              (unsigned)len, (long)st);
            }
        }
    }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *s) override {
        g_connected = true;
        mcl_ble_reassembler_reset(&g_reasm);
        Serial.println("MCLBLE CONNECTED");
    }
    void onDisconnect(BLEServer *s) override {
        g_connected = false;
        mcl_ble_reassembler_reset(&g_reasm);
        Serial.println("MCLBLE DISCONNECTED");
        s->startAdvertising();
    }
};

static void handle_serial_line(const String &line)
{
    if (line == "PING") {
        Serial.println("MCLPONG");
    } else if (line == "STATUS") {
        Serial.printf("MCLBLE STATUS conn=%d fragrx=%lu ok=%lu rej=%lu reasmerr=%lu fragtx=%lu logdrop=%lu\n",
                      g_connected ? 1 : 0,
                      (unsigned long)g_frag_rx, (unsigned long)g_frames_ok,
                      (unsigned long)g_frames_rej, (unsigned long)g_reasm_err,
                      (unsigned long)g_frag_tx, (unsigned long)g_log_dropped);
    } else if (line == "RESET") {
        g_frag_rx = 0; g_frames_ok = 0; g_frames_rej = 0;
        g_reasm_err = 0; g_frag_tx = 0; g_log_dropped = 0;
        g_tx_sequence = 0;
        mcl_ble_reassembler_reset(&g_reasm);
        Serial.println("MCLBLE RESET OK");
    }
}

void setup()
{
    uint8_t adv_frame[64];
    size_t adv_len;

    Serial.begin(115200);
    delay(300);

    mcl_ble_reassembler_reset(&g_reasm);

    BLEDevice::init("MCL-BLE-TEST");
    g_server = BLEDevice::createServer();
    g_server->setCallbacks(new ServerCallbacks());

    BLEService *service = g_server->createService(MCL_SERVICE_UUID);

    BLECharacteristic *rx = service->createCharacteristic(
        MCL_RX_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new RxCallbacks());

    g_tx_char = service->createCharacteristic(
        MCL_TX_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    g_tx_char->addDescriptor(new BLE2902());

    service->start();

    /*
     * Connectionless presence. A Tier-0 PRESENCE Link frame is small enough to
     * ride in advertising data, which is how a machine announces itself before
     * any connection exists. The binding is asked whether it fits rather than
     * being trusted to.
     */
    adv_len = build_presence_frame(adv_frame, sizeof(adv_frame), 0u,
                                   MCL_LINK_CLASS_CONTACT, 0u);
    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(MCL_SERVICE_UUID);

    if (adv_len > 0 && mcl_ble_fits_advertisement(adv_len)) {
        BLEAdvertisementData adv;
        String mfg;
        /* 0xFFFF is the reserved company identifier for internal test use. */
        mfg += (char)0xFF;
        mfg += (char)0xFF;
        for (size_t i = 0; i < adv_len; ++i) { mfg += (char)adv_frame[i]; }
        adv.setManufacturerData(mfg);
        advertising->setScanResponseData(adv);
        Serial.printf("MCLBLE ADV frame=%u bytes carried in scan response\n",
                      (unsigned)adv_len);
    } else {
        Serial.printf("MCLBLE ADV frame does not fit advertising data (%u bytes)\n",
                      (unsigned)adv_len);
    }

    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.printf("MCLBLE READY name=MCL-BLE-TEST svc=%s mtu=%u perpdu=%u frame_max=%u\n",
                  MCL_SERVICE_UUID, (unsigned)MCL_BLE_ATT_DEFAULT_MTU,
                  (unsigned)mcl_ble_payload_per_pdu(MCL_BLE_ATT_DEFAULT_MTU),
                  (unsigned)MCL_LINK_FRAME_MAX_SIZE);
}

void loop()
{
    while (Serial.available() > 0) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            handle_serial_line(line);
        }
    }
    delay(10);
}
