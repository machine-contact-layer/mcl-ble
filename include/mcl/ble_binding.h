#ifndef MCL_BLE_BINDING_H
#define MCL_BLE_BINDING_H

#include <stddef.h>
#include <stdint.h>

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

/* ---------- Reassembly ---------- */

#define MCL_BLE_REASSEMBLY_MAX_FRAME 1024u

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

/* ---------- Connectionless presence ---------- */

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
