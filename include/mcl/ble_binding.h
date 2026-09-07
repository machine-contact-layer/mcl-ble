#ifndef MCL_BLE_BINDING_H
#define MCL_BLE_BINDING_H

#include <stddef.h>
#include <stdint.h>

#include "mcl/link.h"         /* the carriage unit this binding fragments */
#include "mcl/endpoint_rendezvous.h"   /* the beacon this binding places in advertising data */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MCL-BLE binding v0 (research draft).
 *
 * Carriage of MCL Link frames over Bluetooth Low Energy, and the endpoint
 * representation an MCL TRANSPORT_OFFER references.
 *
 * The problem BLE poses that IP does not is size. An ATT payload is 20 bytes
 * at the default MTU and often no more than 244 even after negotiation, while
 * advertising data is 31 bytes. A Link frame carrying a Tier-0 object is
 * around 20 bytes and fits; anything richer does not. So this binding owns a
 * fragmentation and reassembly scheme, and owns it explicitly rather than
 * leaving each integrator to invent one.
 *
 * Contains no Bluetooth stack, no GATT implementation and no OS calls.
 * Freestanding C99, no allocation, no global mutable state.
 */

typedef int32_t mcl_ble_status_t;
enum {
    MCL_BLE_OK = 0,
    MCL_BLE_ERR_INVALID_ARGUMENT = 1,
    MCL_BLE_ERR_RANGE = 2,
    MCL_BLE_ERR_TRUNCATED = 3,
    MCL_BLE_ERR_NONCANONICAL = 4,
    MCL_BLE_ERR_REASSEMBLY = 5,
    MCL_BLE_ERR_INCOMPLETE = 6
};

/* Transport identifier assigned to this binding in mcl-link. */
#define MCL_BLE_TRANSPORT_ID 0x03u

/* ---------- BLE constants that bound the design ---------- */

#define MCL_BLE_ATT_DEFAULT_MTU     23u   /* 20 bytes of payload after ATT header */
#define MCL_BLE_ATT_HEADER_SIZE     3u
#define MCL_BLE_ADV_DATA_MAX_SIZE   31u
#define MCL_BLE_ADDRESS_SIZE        6u

/* ---------- Roles and address types ---------- */

typedef uint8_t mcl_ble_role_t;
enum {
    MCL_BLE_ROLE_PERIPHERAL = 0u,
    MCL_BLE_ROLE_CENTRAL    = 1u,
    /* Connectionless: presence carried in advertising data only. */
    MCL_BLE_ROLE_BROADCAST  = 2u,
    MCL_BLE_ROLE_COUNT      = 3u
};

typedef uint8_t mcl_ble_address_type_t;
enum {
    MCL_BLE_ADDR_PUBLIC          = 0u,
    MCL_BLE_ADDR_RANDOM_STATIC   = 1u,
    /*
     * A resolvable private address rotates by design. It is a privacy
     * mechanism, and MCL must never treat it as a stable identifier or as
     * evidence of identity.
     */
    MCL_BLE_ADDR_RANDOM_PRIVATE  = 2u,
    MCL_BLE_ADDR_TYPE_COUNT      = 3u
};

/*
 * Endpoint offer referenced by an MCL TRANSPORT_OFFER.
 *
 * Canonical encoding:
 *
 *   u8   role
 *   u8   address_type
 *   u8   address[6]
 *   u16  att_mtu           negotiated or expected, >= 23
 *   u16  service_ref       integrator-scoped service/characteristic reference
 *   u16  validity_s        0 means unspecified, not infinite
 */
typedef struct {
    mcl_ble_role_t role;
    mcl_ble_address_type_t address_type;
    uint8_t address[MCL_BLE_ADDRESS_SIZE];
    uint16_t att_mtu;
    uint16_t service_ref;
    uint16_t validity_s;
} mcl_ble_endpoint_t;

#define MCL_BLE_ENDPOINT_SIZE 14u

mcl_ble_status_t mcl_ble_endpoint_encode(
    const mcl_ble_endpoint_t *endpoint,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

mcl_ble_status_t mcl_ble_endpoint_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_ble_endpoint_t *endpoint,
    size_t *consumed);

/* Usable payload per ATT write for a given MTU. */
size_t mcl_ble_payload_per_pdu(uint16_t att_mtu);

/* ---------- Fragmentation ---------- */

/*
 * Every fragment carries a one-byte header:
 *
 *   bit 7      START  — first fragment of a frame
 *   bit 6      END    — last fragment of a frame
 *   bits 0..5  SEQ    — fragment sequence, wrapping modulo 64
 *
 * A frame that fits one PDU sets START and END together. The sequence lets a
 * receiver detect a lost or reordered fragment rather than silently splicing
 * unrelated bytes into one frame, which is the failure that turns a dropped
 * packet into a corrupt semantic object.
 */
#define MCL_BLE_FRAG_HEADER_SIZE 1u
#define MCL_BLE_FRAG_START       0x80u
#define MCL_BLE_FRAG_END         0x40u
#define MCL_BLE_FRAG_SEQ_MASK    0x3Fu
#define MCL_BLE_FRAG_SEQ_MODULUS 64u

/* Number of fragments a frame of this size needs at this MTU, or 0 if impossible. */
size_t mcl_ble_fragment_count(size_t frame_size, uint16_t att_mtu);

/*
 * Produce fragment `index` of `frame`. Returns MCL_BLE_ERR_RANGE when the
 * index is past the end.
 */
mcl_ble_status_t mcl_ble_fragment(
    const uint8_t *frame,
    size_t frame_size,
    uint16_t att_mtu,
    size_t index,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

/* Usable payload per PDU at the smallest MTU BLE permits: 23 - 3 - 1. */
#define MCL_BLE_MIN_PAYLOAD_PER_PDU     (MCL_BLE_ATT_DEFAULT_MTU - MCL_BLE_ATT_HEADER_SIZE - MCL_BLE_FRAG_HEADER_SIZE)

/* Worst case: the largest legal Link frame at the smallest permitted MTU. */
#define MCL_BLE_MAX_FRAGMENTS     ((MCL_LINK_FRAME_MAX_SIZE + MCL_BLE_MIN_PAYLOAD_PER_PDU - 1u)      / MCL_BLE_MIN_PAYLOAD_PER_PDU)

/*
 * The six-bit sequence must not wrap within a single frame.
 *
 * If it could, a receiver that missed exactly one modulus of fragments would
 * see the sequence it expected and splice unrelated bytes into the middle of a
 * frame. Reassembly cannot detect that, so the safety of the whole scheme rests
 * on this inequality holding rather than on a runtime check. It is asserted at
 * compile time so that raising the frame limit or lowering the MTU cannot break
 * it silently.
 */
typedef char mcl_ble_sequence_cannot_wrap_within_a_frame[
    (MCL_BLE_MAX_FRAGMENTS <= MCL_BLE_FRAG_SEQ_MODULUS) ? 1 : -1];

/* ---------- Reassembly ---------- */

/*
 * Sized to the largest frame Link can produce, not to a number chosen here.
 * A bound below MCL_LINK_FRAME_MAX_SIZE would make this binding unable to
 * carry a legal frame, and the failure would only show up on large payloads.
 */
#define MCL_BLE_REASSEMBLY_MAX_FRAME MCL_LINK_FRAME_MAX_SIZE

typedef struct {
    uint8_t buffer[MCL_BLE_REASSEMBLY_MAX_FRAME];
    size_t length;
    uint8_t active;        /* a START has been seen and no END yet */
    uint8_t next_seq;      /* sequence expected on the next fragment */
} mcl_ble_reassembler_t;

void mcl_ble_reassembler_reset(mcl_ble_reassembler_t *r);

/*
 * Feed one fragment.
 *
 * Returns MCL_BLE_OK when a complete frame is available, and writes its length
 * to *frame_size; the bytes are in r->buffer. Returns MCL_BLE_ERR_INCOMPLETE
 * when more fragments are needed, which is the normal case and not an error
 * condition. Any sequence gap, unexpected START, fragment without a preceding
 * START, or overflow resets the reassembler and returns MCL_BLE_ERR_REASSEMBLY:
 * a partial frame is discarded rather than delivered.
 */
mcl_ble_status_t mcl_ble_reassemble(
    mcl_ble_reassembler_t *r,
    const uint8_t *fragment,
    size_t fragment_size,
    size_t *frame_size);

/*
 * Profile conformance check for a reassembled frame.
 *
 * spec/ble-gatt-profile-v1.md section 6. Run this on the bytes the
 * reassembler produced, BEFORE handing them to Link.
 *
 * WHY THE FRAME CHECK IS REQUIRED HERE
 *
 * BLE's link layer already has a 24-bit CRC and connected GATT is acknowledged
 * and retransmitted, so radio corruption reaching this layer is unlikely. The
 * risk is not the radio -- it is the REASSEMBLY. A frame is split across up to
 * 56 PDUs, each individually protected by the link-layer CRC and each
 * individually correct; a lost, duplicated, reordered or mis-spliced FRAGMENT
 * produces a corrupt frame out of perfectly valid PDUs, and no amount of
 * link-layer integrity can see that. The fragment sequence catches most of it.
 * The frame check is what catches the rest.
 *
 * Checked after reassembly, over the whole frame. Checking per fragment would
 * re-verify what the link layer already verified and miss the only failure mode
 * that matters.
 *
 * It is a CRC: accidental corruption only, not integrity in the security sense.
 * Anyone who can write to the medium can recompute it.
 */
mcl_ble_status_t mcl_ble_frame_validate(
    const uint8_t *frame,
    size_t frame_size);

/* ---------- Connectionless presence ---------- */

/* ---------- Endpoint rendezvous ----------
 *
 * Where the transport-neutral rendezvous beacon (mcl-link/rendezvous.h) is
 * placed in BLE: advertising Service Data for the MCL 128-bit service UUID.
 *
 *   AD structure:  length(1) | type 0x21 | UUID(16, little-endian) | beacon(8)
 *                  = 26 bytes of the 31 available
 *
 * A 128-bit UUID is used rather than a 16-bit one because 16-bit UUIDs are
 * allocated by the Bluetooth SIG and this project has not been assigned one.
 * Squatting on an unassigned 16-bit value would collide with whoever is later
 * assigned it. Manufacturer Specific Data is likewise unavailable, since it
 * requires a company identifier we do not hold. A production deployment that
 * holds either should define a profile that uses it and save 14 bytes; this is
 * the form that is correct without owning an allocation.
 *
 * 26 bytes leaves 5, which is enough for the 3-byte Flags AD structure that
 * most stacks insert, and not enough for much else. Advertising space is the
 * binding's scarcest resource and this uses most of it, which is the reason
 * addresses are NOT advertised: the scanner learns the address from the
 * advertisement it received, not from the token.
 */
#define MCL_BLE_AD_TYPE_SERVICE_DATA_128 0x21u
#define MCL_BLE_SERVICE_UUID_SIZE        16u

/*
 * MCL BLE service UUID 6d636c00-0001-4d43-4c00-6d636c626c65, in the
 * little-endian order Bluetooth advertising data uses. The bytes spell "mcl"
 * and "mclble" in the fixed fields, which makes an advertisement recognisable
 * in a raw capture without a decoder.
 */
#define MCL_BLE_SERVICE_UUID_BYTES {     0x65u, 0x6Cu, 0x62u, 0x6Cu, 0x63u, 0x6Du, 0x00u, 0x4Cu,     0x43u, 0x4Du, 0x01u, 0x00u, 0x00u, 0x6Cu, 0x63u, 0x6Du }

/* length + type + UUID + beacon. */
#define MCL_BLE_RENDEZVOUS_AD_SIZE     (2u + MCL_BLE_SERVICE_UUID_SIZE + MCL_RENDEZVOUS_BEACON_SIZE)

/*
 * Build the complete advertising AD structure carrying a rendezvous beacon.
 * `out` receives MCL_BLE_RENDEZVOUS_AD_SIZE bytes, ready to concatenate into
 * advertising data.
 */
mcl_ble_status_t mcl_ble_rendezvous_ad_encode(
    uint32_t endpoint_token,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

/*
 * Scan-side counterpart: does this observed advertising payload contain a
 * rendezvous beacon for `expected_token`?
 *
 * Takes the whole advertising data and walks its AD structures, because a real
 * scan result contains Flags, a name and whatever else the peer advertises, in
 * an order nothing guarantees. Returns 0 for anything that is not a match,
 * including malformed data: a scanner sees unrelated advertisements constantly
 * and that is not an error.
 */
uint8_t mcl_ble_rendezvous_ad_matches(
    const uint8_t *adv_data,
    size_t adv_size,
    uint32_t expected_token);

/*
 * A Link frame small enough to fit advertising data may be broadcast without a
 * connection, which is how a machine announces presence to peers it has never
 * met. Returns 1 when the frame fits, 0 otherwise.
 */
uint8_t mcl_ble_fits_advertisement(size_t frame_size);

#ifdef __cplusplus
}
#endif

#endif /* MCL_BLE_BINDING_H */
