/*
 * MCL-BLE binding v0 tests.
 *
 * Endpoint canonicalisation, and the fragmentation scheme this binding exists
 * for: exact round trips at hostile MTUs, and the loss and reordering cases
 * that must discard rather than splice.
 */

#include "mcl/ble_binding.h"
#include "mcl/link.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do {                                      \
    ++tests_run;                                                   \
    if (!(cond)) {                                                 \
        ++tests_failed;                                            \
        printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    }                                                              \
} while (0)

static const uint8_t k_presence[] = {
    0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x00u, 0x00u, 0x01u, 0x3Cu
};

static size_t build_frame(uint8_t *out, size_t capacity, size_t payload_len)
{
    static uint8_t payload[MCL_BLE_REASSEMBLY_MAX_FRAME];
    mcl_link_frame_t f;
    size_t written = 0u, i;

    for (i = 0u; i < payload_len; ++i) {
        payload[i] = (uint8_t)(i & 0xFFu);
    }

    memset(&f, 0, sizeof(f));
    f.frame_class = MCL_LINK_CLASS_DATA;
    f.source_ref = 0x0B1Eu;
    f.payload = (payload_len != 0u) ? payload : NULL;
    f.payload_len = (uint16_t)payload_len;

    if (mcl_link_frame_encode(&f, out, capacity, &written) != MCL_LINK_OK) {
        return 0u;
    }
    return written;
}

static void test_endpoint_round_trip(void)
{
    mcl_ble_endpoint_t tx, rx;
    uint8_t buf[32];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] endpoint round trip\n");

    memset(&tx, 0, sizeof(tx));
    tx.role = MCL_BLE_ROLE_PERIPHERAL;
    tx.address_type = MCL_BLE_ADDR_RANDOM_PRIVATE;
    tx.address[0] = 0xC0u; tx.address[5] = 0x0Fu;
    tx.att_mtu = 247u;
    tx.service_ref = 0xFE10u;
    tx.validity_s = 120u;

    CHECK(mcl_ble_endpoint_encode(&tx, buf, sizeof(buf), &written) == MCL_BLE_OK,
          "encode");
    CHECK(written == MCL_BLE_ENDPOINT_SIZE, "fixed endpoint size");
    CHECK(mcl_ble_endpoint_decode(buf, written, &rx, &consumed) == MCL_BLE_OK,
          "decode");
    CHECK(consumed == written, "consumed all");
    CHECK(rx.att_mtu == 247u, "mtu preserved");
    CHECK(rx.service_ref == 0xFE10u, "service ref preserved");
    CHECK(rx.address_type == MCL_BLE_ADDR_RANDOM_PRIVATE, "address type preserved");
    CHECK(memcmp(rx.address, tx.address, MCL_BLE_ADDRESS_SIZE) == 0,
          "address preserved");
}

static void test_endpoint_rejects_bad_values(void)
{
    mcl_ble_endpoint_t ep;
    uint8_t buf[32];
    size_t written = 0u, consumed = 0u, n;

    printf("[TEST] endpoint rejects unknown role, type and sub-minimum MTU\n");

    memset(&ep, 0, sizeof(ep));
    ep.role = MCL_BLE_ROLE_CENTRAL;
    ep.address_type = MCL_BLE_ADDR_PUBLIC;
    ep.att_mtu = MCL_BLE_ATT_DEFAULT_MTU;
    CHECK(mcl_ble_endpoint_encode(&ep, buf, sizeof(buf), &written) == MCL_BLE_OK,
          "baseline encodes");

    ep.att_mtu = 22u;   /* below the BLE minimum */
    CHECK(mcl_ble_endpoint_encode(&ep, buf, sizeof(buf), &written) == MCL_BLE_ERR_RANGE,
          "sub-minimum MTU not encodable");

    ep.att_mtu = MCL_BLE_ATT_DEFAULT_MTU;
    ep.role = (mcl_ble_role_t)MCL_BLE_ROLE_COUNT;
    CHECK(mcl_ble_endpoint_encode(&ep, buf, sizeof(buf), &written) == MCL_BLE_ERR_RANGE,
          "unknown role not encodable");

    ep.role = MCL_BLE_ROLE_CENTRAL;
    (void)mcl_ble_endpoint_encode(&ep, buf, sizeof(buf), &written);
    buf[0] = (uint8_t)MCL_BLE_ROLE_COUNT;
    CHECK(mcl_ble_endpoint_decode(buf, written, &ep, &consumed) == MCL_BLE_ERR_NONCANONICAL,
          "unknown role rejected on decode");

    (void)mcl_ble_endpoint_encode(&ep, buf, sizeof(buf), &written);
    buf[1] = (uint8_t)MCL_BLE_ADDR_TYPE_COUNT;
    CHECK(mcl_ble_endpoint_decode(buf, written, &ep, &consumed) == MCL_BLE_ERR_NONCANONICAL,
          "unknown address type rejected on decode");

    (void)mcl_ble_endpoint_encode(&ep, buf, sizeof(buf), &written);
    for (n = 0u; n < written; ++n) {
        CHECK(mcl_ble_endpoint_decode(buf, n, &ep, &consumed) == MCL_BLE_ERR_TRUNCATED,
              "short endpoint reports truncation");
    }
}

static void test_payload_accounting(void)
{
    printf("[TEST] payload accounting at the default MTU\n");

    /* 23 - 3 ATT - 1 fragment header = 19 usable bytes per PDU. */
    CHECK(mcl_ble_payload_per_pdu(MCL_BLE_ATT_DEFAULT_MTU) == 19u,
          "default MTU yields 19 payload bytes");
    CHECK(mcl_ble_payload_per_pdu(22u) == 0u, "below minimum yields nothing");
    CHECK(mcl_ble_payload_per_pdu(247u) == 243u, "negotiated MTU yields 243");

    CHECK(mcl_ble_fragment_count(19u, MCL_BLE_ATT_DEFAULT_MTU) == 1u,
          "exactly one PDU");
    CHECK(mcl_ble_fragment_count(20u, MCL_BLE_ATT_DEFAULT_MTU) == 2u,
          "one byte over needs two PDUs");
    CHECK(mcl_ble_fragment_count(0u, MCL_BLE_ATT_DEFAULT_MTU) == 0u,
          "empty frame has no fragments");
    CHECK(mcl_ble_fragment_count(MCL_BLE_REASSEMBLY_MAX_FRAME + 1u,
                                 MCL_BLE_ATT_DEFAULT_MTU) == 0u,
          "oversize frame refused");
}

static void round_trip_at_mtu(uint16_t mtu, size_t payload_len, const char *label)
{
    uint8_t frame[MCL_BLE_REASSEMBLY_MAX_FRAME];
    uint8_t pdu[512];
    mcl_ble_reassembler_t r;
    size_t frame_size, count, i, written = 0u, out_size = 0u;
    mcl_ble_status_t st = MCL_BLE_ERR_INCOMPLETE;

    frame_size = build_frame(frame, sizeof(frame), payload_len);
    CHECK(frame_size > 0u, label);

    count = mcl_ble_fragment_count(frame_size, mtu);
    CHECK(count > 0u, "fragment count positive");

    mcl_ble_reassembler_reset(&r);
    for (i = 0u; i < count; ++i) {
        CHECK(mcl_ble_fragment(frame, frame_size, mtu, i, pdu, sizeof(pdu), &written)
                  == MCL_BLE_OK, "fragment produced");
        CHECK(written <= (size_t)mtu - MCL_BLE_ATT_HEADER_SIZE,
              "fragment fits the ATT payload");
        st = mcl_ble_reassemble(&r, pdu, written, &out_size);
        if (i + 1u < count) {
            CHECK(st == MCL_BLE_ERR_INCOMPLETE, "intermediate fragment incomplete");
        }
    }
    CHECK(st == MCL_BLE_OK, "final fragment completes the frame");
    CHECK(out_size == frame_size, "reassembled length matches");
    CHECK(memcmp(r.buffer, frame, frame_size) == 0, "reassembled bytes exact");
}

static void test_fragmentation_round_trips(void)
{
    printf("[TEST] fragmentation round trips at hostile MTUs\n");

    round_trip_at_mtu(MCL_BLE_ATT_DEFAULT_MTU, sizeof(k_presence),
                      "Tier-0 frame at default MTU");
    round_trip_at_mtu(MCL_BLE_ATT_DEFAULT_MTU, 1u, "one-byte payload");
    round_trip_at_mtu(MCL_BLE_ATT_DEFAULT_MTU, 11u, "presence-sized payload");
    round_trip_at_mtu(MCL_BLE_ATT_DEFAULT_MTU, 200u, "multi-fragment payload");
    round_trip_at_mtu(247u, 500u, "large payload at negotiated MTU");
    /* Exactly on a fragment boundary, where an off-by-one would show. */
    round_trip_at_mtu(MCL_BLE_ATT_DEFAULT_MTU, 19u - MCL_LINK_FRAME_MIN_SIZE,
                      "frame exactly one PDU long");
}

static void test_reassembly_rejects_gap(void)
{
    uint8_t frame[256];
    uint8_t pdu[64];
    mcl_ble_reassembler_t r;
    size_t frame_size, count, written = 0u, out_size = 0u;

    printf("[TEST] a sequence gap discards rather than splices\n");

    frame_size = build_frame(frame, sizeof(frame), 100u);
    count = mcl_ble_fragment_count(frame_size, MCL_BLE_ATT_DEFAULT_MTU);
    CHECK(count >= 3u, "enough fragments to drop one");

    mcl_ble_reassembler_reset(&r);
    (void)mcl_ble_fragment(frame, frame_size, MCL_BLE_ATT_DEFAULT_MTU, 0u,
                           pdu, sizeof(pdu), &written);
    CHECK(mcl_ble_reassemble(&r, pdu, written, &out_size) == MCL_BLE_ERR_INCOMPLETE,
          "first fragment accepted");

    /* Skip fragment 1 and deliver fragment 2. */
    (void)mcl_ble_fragment(frame, frame_size, MCL_BLE_ATT_DEFAULT_MTU, 2u,
                           pdu, sizeof(pdu), &written);
    CHECK(mcl_ble_reassemble(&r, pdu, written, &out_size) == MCL_BLE_ERR_REASSEMBLY,
          "gap detected");
    CHECK(r.active == 0u, "reassembler reset after a gap");
    CHECK(r.length == 0u, "partial bytes discarded");
}

static void test_reassembly_rejects_orphan_and_restart(void)
{
    uint8_t frame[256];
    uint8_t pdu[64];
    mcl_ble_reassembler_t r;
    size_t frame_size, written = 0u, out_size = 0u;

    printf("[TEST] orphaned continuation and mid-frame restart\n");

    frame_size = build_frame(frame, sizeof(frame), 100u);

    /* A continuation with no START has nothing to join. */
    mcl_ble_reassembler_reset(&r);
    (void)mcl_ble_fragment(frame, frame_size, MCL_BLE_ATT_DEFAULT_MTU, 1u,
                           pdu, sizeof(pdu), &written);
    CHECK(mcl_ble_reassemble(&r, pdu, written, &out_size) == MCL_BLE_ERR_REASSEMBLY,
          "orphaned continuation rejected");

    /* A START mid-frame abandons the previous partial frame. */
    mcl_ble_reassembler_reset(&r);
    (void)mcl_ble_fragment(frame, frame_size, MCL_BLE_ATT_DEFAULT_MTU, 0u,
                           pdu, sizeof(pdu), &written);
    (void)mcl_ble_reassemble(&r, pdu, written, &out_size);
    CHECK(r.length > 0u, "partial frame in progress");

    (void)mcl_ble_fragment(frame, frame_size, MCL_BLE_ATT_DEFAULT_MTU, 0u,
                           pdu, sizeof(pdu), &written);
    CHECK(mcl_ble_reassemble(&r, pdu, written, &out_size) == MCL_BLE_ERR_INCOMPLETE,
          "restart begins a fresh frame");
    CHECK(r.length == written - MCL_BLE_FRAG_HEADER_SIZE,
          "only the new fragment is held, the old partial is gone");
}

static void test_reassembly_rejects_malformed(void)
{
    mcl_ble_reassembler_t r;
    uint8_t pdu[8];
    size_t out_size = 0u;

    printf("[TEST] malformed fragments rejected\n");

    mcl_ble_reassembler_reset(&r);

    /* Header with no payload carries nothing. */
    pdu[0] = MCL_BLE_FRAG_START;
    CHECK(mcl_ble_reassemble(&r, pdu, 1u, &out_size) == MCL_BLE_ERR_REASSEMBLY,
          "header-only fragment rejected");

    /* A START whose sequence is not zero is non-canonical. */
    mcl_ble_reassembler_reset(&r);
    pdu[0] = (uint8_t)(MCL_BLE_FRAG_START | 5u);
    pdu[1] = 0xAAu;
    CHECK(mcl_ble_reassemble(&r, pdu, 2u, &out_size) == MCL_BLE_ERR_REASSEMBLY,
          "START with nonzero sequence rejected");

    CHECK(mcl_ble_reassemble(NULL, pdu, 2u, &out_size) == MCL_BLE_ERR_INVALID_ARGUMENT,
          "null reassembler rejected");
    CHECK(mcl_ble_reassemble(&r, NULL, 2u, &out_size) == MCL_BLE_ERR_INVALID_ARGUMENT,
          "null fragment rejected");
}

static void test_advertisement_fit(void)
{
    uint8_t frame[64];
    size_t frame_size;

    printf("[TEST] connectionless presence fits advertising data\n");

    frame_size = build_frame(frame, sizeof(frame), sizeof(k_presence));
    CHECK(frame_size <= MCL_BLE_ADV_DATA_MAX_SIZE,
          "a Tier-0 Link frame fits a BLE advertisement");
    CHECK(mcl_ble_fits_advertisement(frame_size) == 1u, "reported as fitting");
    CHECK(mcl_ble_fits_advertisement(MCL_BLE_ADV_DATA_MAX_SIZE + 1u) == 0u,
          "oversize does not fit");
    CHECK(mcl_ble_fits_advertisement(0u) == 0u, "empty does not fit");
}

/*
 * The binding must carry the largest frame Link can legally produce, at the
 * smallest MTU BLE permits. This is the case a reassembly limit chosen
 * independently of Link would silently fail, and it is also the case that
 * comes closest to exhausting the six-bit fragment sequence.
 */
static void test_carries_a_maximal_link_frame(void)
{
    static uint8_t payload[MCL_LINK_FRAME_MAX_PAYLOAD];
    static uint8_t frame[MCL_LINK_FRAME_MAX_SIZE];
    static mcl_ble_reassembler_t r;
    mcl_link_frame_t tx;
    uint8_t pdu[MCL_BLE_ATT_DEFAULT_MTU];
    size_t written = 0u, count, i, produced = 0u, done = 0u;
    mcl_ble_status_t st = MCL_BLE_ERR_INCOMPLETE;

    printf("[TEST] a maximal Link frame survives fragmentation at the minimum MTU\n");

    for (i = 0u; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i * 31u + 7u);
    }

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.flags = (uint8_t)(MCL_LINK_FLAG_DESTINATION | MCL_LINK_FLAG_SESSION |
                         MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRESHNESS |
                         MCL_LINK_FLAG_INTEGRITY);
    tx.source_ref = 0x01020304u;
    tx.payload = payload;
    tx.payload_len = (uint16_t)sizeof(payload);

    CHECK(mcl_link_frame_encode(&tx, frame, sizeof(frame), &written) == MCL_LINK_OK,
          "maximal frame encodes");
    CHECK(written == (size_t)MCL_LINK_FRAME_MAX_SIZE,
          "maximal frame is exactly MCL_LINK_FRAME_MAX_SIZE");

    count = mcl_ble_fragment_count(written, MCL_BLE_ATT_DEFAULT_MTU);
    CHECK(count > 0u, "a maximal frame is fragmentable at the default MTU");
    CHECK(count <= (size_t)MCL_BLE_FRAG_SEQ_MODULUS,
          "the fragment sequence does not wrap within one frame");

    mcl_ble_reassembler_reset(&r);
    for (i = 0u; i < count; ++i) {
        CHECK(mcl_ble_fragment(frame, written, MCL_BLE_ATT_DEFAULT_MTU, i,
                               pdu, sizeof(pdu), &produced) == MCL_BLE_OK,
              "fragment produced");
        CHECK(produced <= (size_t)(MCL_BLE_ATT_DEFAULT_MTU - MCL_BLE_ATT_HEADER_SIZE),
              "fragment fits one ATT payload");
        st = mcl_ble_reassemble(&r, pdu, produced, &done);
        if (i + 1u < count) {
            CHECK(st == MCL_BLE_ERR_INCOMPLETE, "intermediate fragment is incomplete");
        }
    }

    CHECK(st == MCL_BLE_OK, "final fragment completes the frame");
    CHECK(done == written, "reassembled length matches");
    CHECK(memcmp(r.buffer, frame, written) == 0, "reassembled bytes are identical");
}

int main(void)
{
    printf("MCL-BLE binding v0 tests\n");
    printf("========================\n");

    test_endpoint_round_trip();
    test_endpoint_rejects_bad_values();
    test_payload_accounting();
    test_fragmentation_round_trips();
    test_reassembly_rejects_gap();
    test_reassembly_rejects_orphan_and_restart();
    test_reassembly_rejects_malformed();
    test_advertisement_fit();
    test_carries_a_maximal_link_frame();

    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
