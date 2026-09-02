/*
 * MCL-BLE binding v0 reference implementation.
 *
 * Freestanding C99. No Bluetooth stack, no allocation, no global mutable state.
 */

#include "mcl/ble_binding.h"

/*
 * Byte copy that the compiler may not rewrite into a call to memcpy.
 *
 * A plain indexed loop is pattern-matched into memcpy by every optimising
 * compiler, which reintroduces a libc dependency into an object that must link
 * on a freestanding target with no C library present. The volatile destination
 * defeats that rewrite. mcl-wire solves the same problem the same way.
 */
static void mcl_ble_copy_bytes(uint8_t *dst, const uint8_t *src, size_t count)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    size_t i;

    for (i = 0u; i < count; ++i) {
        out[i] = src[i];
    }
}

static uint8_t mcl_ble_endpoint_valid(const mcl_ble_endpoint_t *endpoint)
{
    if (endpoint == NULL) {
        return 0u;
    }
    if (endpoint->role >= MCL_BLE_ROLE_COUNT) {
        return 0u;
    }
    if (endpoint->address_type >= MCL_BLE_ADDR_TYPE_COUNT) {
        return 0u;
    }
    /* An MTU below the BLE minimum is not a smaller link, it is a malformed offer. */
    if (endpoint->att_mtu < MCL_BLE_ATT_DEFAULT_MTU) {
        return 0u;
    }
    return 1u;
}

mcl_ble_status_t mcl_ble_endpoint_encode(
    const mcl_ble_endpoint_t *endpoint,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    size_t pos = 0u;

    if (endpoint == NULL || out == NULL || written == NULL) {
        return MCL_BLE_ERR_INVALID_ARGUMENT;
    }
    if (mcl_ble_endpoint_valid(endpoint) == 0u) {
        return MCL_BLE_ERR_RANGE;
    }
    if (out_capacity < (size_t)MCL_BLE_ENDPOINT_SIZE) {
        return MCL_BLE_ERR_RANGE;
    }

    out[pos++] = endpoint->role;
    out[pos++] = endpoint->address_type;
    mcl_ble_copy_bytes(out + pos, endpoint->address, (size_t)MCL_BLE_ADDRESS_SIZE);
    pos += (size_t)MCL_BLE_ADDRESS_SIZE;

    out[pos++] = (uint8_t)(endpoint->att_mtu >> 8u);
    out[pos++] = (uint8_t)(endpoint->att_mtu & 0xFFu);
    out[pos++] = (uint8_t)(endpoint->service_ref >> 8u);
    out[pos++] = (uint8_t)(endpoint->service_ref & 0xFFu);
    out[pos++] = (uint8_t)(endpoint->validity_s >> 8u);
    out[pos++] = (uint8_t)(endpoint->validity_s & 0xFFu);

    *written = pos;
    return MCL_BLE_OK;
}

mcl_ble_status_t mcl_ble_endpoint_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_ble_endpoint_t *endpoint,
    size_t *consumed)
{
    size_t pos = 0u;

    if (in == NULL || endpoint == NULL || consumed == NULL) {
        return MCL_BLE_ERR_INVALID_ARGUMENT;
    }
    if (in_size < (size_t)MCL_BLE_ENDPOINT_SIZE) {
        return MCL_BLE_ERR_TRUNCATED;
    }

    endpoint->role = in[pos++];
    endpoint->address_type = in[pos++];
    mcl_ble_copy_bytes(endpoint->address, in + pos, (size_t)MCL_BLE_ADDRESS_SIZE);
    pos += (size_t)MCL_BLE_ADDRESS_SIZE;

    endpoint->att_mtu = (uint16_t)(((uint16_t)in[pos] << 8u) | (uint16_t)in[pos + 1u]);
    pos += 2u;
    endpoint->service_ref = (uint16_t)(((uint16_t)in[pos] << 8u) | (uint16_t)in[pos + 1u]);
    pos += 2u;
    endpoint->validity_s = (uint16_t)(((uint16_t)in[pos] << 8u) | (uint16_t)in[pos + 1u]);
    pos += 2u;

    if (mcl_ble_endpoint_valid(endpoint) == 0u) {
        return MCL_BLE_ERR_NONCANONICAL;
    }

    *consumed = pos;
    return MCL_BLE_OK;
}

size_t mcl_ble_payload_per_pdu(uint16_t att_mtu)
{
    size_t usable;

    if (att_mtu < MCL_BLE_ATT_DEFAULT_MTU) {
        return 0u;
    }

    usable = (size_t)att_mtu - (size_t)MCL_BLE_ATT_HEADER_SIZE;
    if (usable <= (size_t)MCL_BLE_FRAG_HEADER_SIZE) {
        return 0u;
    }
    return usable - (size_t)MCL_BLE_FRAG_HEADER_SIZE;
}

size_t mcl_ble_fragment_count(size_t frame_size, uint16_t att_mtu)
{
    size_t per = mcl_ble_payload_per_pdu(att_mtu);

    if (per == 0u || frame_size == 0u) {
        return 0u;
    }
    if (frame_size > (size_t)MCL_BLE_REASSEMBLY_MAX_FRAME) {
        return 0u;
    }
    return (frame_size + per - 1u) / per;
}

mcl_ble_status_t mcl_ble_fragment(
    const uint8_t *frame,
    size_t frame_size,
    uint16_t att_mtu,
    size_t index,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    size_t per, count, offset, chunk;
    uint8_t header;

    if (frame == NULL || out == NULL || written == NULL) {
        return MCL_BLE_ERR_INVALID_ARGUMENT;
    }

    per = mcl_ble_payload_per_pdu(att_mtu);
    count = mcl_ble_fragment_count(frame_size, att_mtu);
    if (per == 0u || count == 0u) {
        return MCL_BLE_ERR_RANGE;
    }
    if (index >= count) {
        return MCL_BLE_ERR_RANGE;
    }

    offset = index * per;
    chunk = frame_size - offset;
    if (chunk > per) {
        chunk = per;
    }
    if (out_capacity < (size_t)MCL_BLE_FRAG_HEADER_SIZE + chunk) {
        return MCL_BLE_ERR_RANGE;
    }

    header = (uint8_t)(index % (size_t)MCL_BLE_FRAG_SEQ_MODULUS);
    if (index == 0u) {
        header |= MCL_BLE_FRAG_START;
    }
    if (index + 1u == count) {
        header |= MCL_BLE_FRAG_END;
    }

    out[0] = header;
    mcl_ble_copy_bytes(out + MCL_BLE_FRAG_HEADER_SIZE, frame + offset, chunk);

    *written = (size_t)MCL_BLE_FRAG_HEADER_SIZE + chunk;
    return MCL_BLE_OK;
}

void mcl_ble_reassembler_reset(mcl_ble_reassembler_t *r)
{
    if (r == NULL) {
        return;
    }
    r->length = 0u;
    r->active = 0u;
    r->next_seq = 0u;
}

mcl_ble_status_t mcl_ble_reassemble(
    mcl_ble_reassembler_t *r,
    const uint8_t *fragment,
    size_t fragment_size,
    size_t *frame_size)
{
    uint8_t header, seq, is_start, is_end;
    size_t payload_size;

    if (r == NULL || fragment == NULL || frame_size == NULL) {
        return MCL_BLE_ERR_INVALID_ARGUMENT;
    }
    if (fragment_size < (size_t)MCL_BLE_FRAG_HEADER_SIZE + 1u) {
        /* A fragment with a header and no payload carries nothing. */
        mcl_ble_reassembler_reset(r);
        return MCL_BLE_ERR_REASSEMBLY;
    }

    header = fragment[0];
    seq = (uint8_t)(header & MCL_BLE_FRAG_SEQ_MASK);
    is_start = (uint8_t)((header & MCL_BLE_FRAG_START) != 0u);
    is_end = (uint8_t)((header & MCL_BLE_FRAG_END) != 0u);
    payload_size = fragment_size - (size_t)MCL_BLE_FRAG_HEADER_SIZE;

    if (is_start != 0u) {
        /*
         * A START always begins a new frame. Any frame already in progress is
         * discarded without being delivered: the peer either restarted or we
         * missed its END, and either way those bytes must never be spliced
         * onto the new frame. The caller learns nothing was delivered because
         * no completion is reported for it.
         */
        mcl_ble_reassembler_reset(r);
        if (seq != 0u) {
            /* A START must open the sequence at zero to be canonical. */
            return MCL_BLE_ERR_REASSEMBLY;
        }
        r->active = 1u;
    } else {
        if (r->active == 0u) {
            /* A continuation with no START is orphaned; there is nothing to join. */
            return MCL_BLE_ERR_REASSEMBLY;
        }
        if (seq != r->next_seq) {
            /* A gap means a lost or reordered fragment. Discard, never splice. */
            mcl_ble_reassembler_reset(r);
            return MCL_BLE_ERR_REASSEMBLY;
        }
    }

    if (r->length + payload_size > (size_t)MCL_BLE_REASSEMBLY_MAX_FRAME) {
        mcl_ble_reassembler_reset(r);
        return MCL_BLE_ERR_REASSEMBLY;
    }

    mcl_ble_copy_bytes(r->buffer + r->length,
                       fragment + MCL_BLE_FRAG_HEADER_SIZE, payload_size);
    r->length += payload_size;
    r->next_seq = (uint8_t)((r->next_seq + 1u) % MCL_BLE_FRAG_SEQ_MODULUS);

    if (is_end != 0u) {
        *frame_size = r->length;
        r->active = 0u;
        return MCL_BLE_OK;
    }

    return MCL_BLE_ERR_INCOMPLETE;
}

uint8_t mcl_ble_fits_advertisement(size_t frame_size)
{
    return (frame_size != 0u && frame_size <= (size_t)MCL_BLE_ADV_DATA_MAX_SIZE)
         ? 1u : 0u;
}
