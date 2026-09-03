/*
 * Flat C entry points over the MCL-BLE binding, for host BLE harnesses.
 *
 * Windows exposes Bluetooth LE scanning and GATT only through WinRT, so a host
 * harness has to be written in a language with WinRT projections. That would
 * normally mean reimplementing framing and fragmentation in that language,
 * which is exactly the mistake that makes an over-air result meaningless: the
 * code being exercised would no longer be the code that ships.
 *
 * This shim exists so it stays the code that ships. It performs no encoding,
 * no fragmentation, no reassembly and no validation of its own. Every function
 * here forwards to mcl-ble, mcl-link or mcl-wire and translates the result
 * into a plain integer the caller can marshal.
 *
 * This is host tooling, not runtime code. It is compiled into a shared library
 * and is not part of the freestanding stack, so it may use a wider ABI than
 * the protocol libraries do. It still keeps reassembly state caller-owned,
 * because a hidden global would make concurrent harnesses quietly wrong.
 */

#include "mcl/ble_binding.h"
#include "mcl/link.h"
#include "mcl/wire.h"

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define MCLX_API __declspec(dllexport)
#else
#  define MCLX_API __attribute__((visibility("default")))
#endif

/* Negative return values are the underlying status, so a caller can report the
 * exact refusal rather than a generic failure. */
#define MCLX_FAIL(status) (-(int32_t)(status))

MCLX_API int32_t mclx_link_frame_max_size(void)
{
    return (int32_t)MCL_LINK_FRAME_MAX_SIZE;
}

MCLX_API int32_t mclx_ble_default_mtu(void)
{
    return (int32_t)MCL_BLE_ATT_DEFAULT_MTU;
}

MCLX_API int32_t mclx_ble_payload_per_pdu(int32_t att_mtu)
{
    if (att_mtu < 0 || att_mtu > 0xFFFF) {
        return 0;
    }
    return (int32_t)mcl_ble_payload_per_pdu((uint16_t)att_mtu);
}

/*
 * Fragment header bit layout, exported so a harness can construct the
 * malformed fragments the reassembly rules exist to refuse. Hardcoding these
 * in the harness would let the two definitions drift, and the test would then
 * silently stop testing what it claims to.
 */
MCLX_API int32_t mclx_frag_start_bit(void)  { return (int32_t)MCL_BLE_FRAG_START; }
MCLX_API int32_t mclx_frag_end_bit(void)    { return (int32_t)MCL_BLE_FRAG_END; }
MCLX_API int32_t mclx_frag_seq_mask(void)   { return (int32_t)MCL_BLE_FRAG_SEQ_MASK; }
MCLX_API int32_t mclx_frag_header_size(void){ return (int32_t)MCL_BLE_FRAG_HEADER_SIZE; }

MCLX_API int32_t mclx_ble_fits_advertisement(int32_t frame_size)
{
    if (frame_size < 0) {
        return 0;
    }
    return (int32_t)mcl_ble_fits_advertisement((size_t)frame_size);
}

/*
 * Build a Link frame carrying one Tier-0 object.
 *
 * `kind` selects the object; a negative kind builds a frame with no payload,
 * which is what a KEEPALIVE or an ACK carries. Returns the encoded length, or
 * a negative status.
 */
MCLX_API int32_t mclx_build_frame(int32_t frame_class,
                                  int32_t flags,
                                  uint32_t source_ref,
                                  int32_t sequence,
                                  int32_t kind,
                                  uint8_t *out,
                                  int32_t out_capacity)
{
    uint8_t wire_buf[MCL_WIRE_TIER0_MAX_SIZE];
    mcl_wire_tier0_t obj;
    mcl_link_frame_t f;
    size_t wire_len = 0u, written = 0u;
    mcl_link_status_t lst;
    size_t i;

    if (out == NULL || out_capacity <= 0) {
        return MCLX_FAIL(MCL_LINK_ERR_INVALID_ARGUMENT);
    }

    for (i = 0u; i < sizeof(f); ++i) { ((uint8_t *)&f)[i] = 0u; }
    f.frame_class = (mcl_link_frame_class_t)frame_class;
    f.flags = (uint8_t)flags;
    f.source_ref = source_ref;
    f.sequence = (uint16_t)sequence;

    if (kind >= 0) {
        mcl_wire_status_t wst;
        for (i = 0u; i < sizeof(obj); ++i) { ((uint8_t *)&obj)[i] = 0u; }
        obj.kind = (mcl_wire_kind_t)kind;
        obj.priority = 2u;
        obj.source_ref = source_ref;

        /* Field widths are the specification's, not this shim's: presence
         * carries a 24-bit capability digest, so a wider value is refused by
         * the encoder rather than silently truncated here. */
        if (obj.kind == MCL_WIRE_KIND_PRESENCE) {
            obj.body.presence.machine_class = 7u;
            obj.body.presence.capability_tag = 0x112233u;
            obj.body.presence.ttl = 60u;
        } else if (obj.kind == MCL_WIRE_KIND_HAZARD) {
            obj.body.hazard.hazard_class = 2u;
            obj.body.hazard.severity = 3u;
            obj.body.hazard.confidence = 80u;
            obj.body.hazard.x = -120;
            obj.body.hazard.y = 340;
            obj.body.hazard.z = 0;
            obj.body.hazard.radius = 250u;
            obj.body.hazard.ttl = 10u;
        } else {
            return MCLX_FAIL(MCL_WIRE_ERR_UNSUPPORTED_SEMANTIC);
        }

        wst = mcl_wire_tier0_encode(&obj, wire_buf, sizeof(wire_buf), &wire_len);
        if (wst != MCL_WIRE_OK) {
            return MCLX_FAIL(wst);
        }
        f.payload = wire_buf;
        f.payload_len = (uint16_t)wire_len;
    }

    lst = mcl_link_frame_encode(&f, out, (size_t)out_capacity, &written);
    if (lst != MCL_LINK_OK) {
        return MCLX_FAIL(lst);
    }
    return (int32_t)written;
}

MCLX_API int32_t mclx_fragment_count(int32_t frame_size, int32_t att_mtu)
{
    if (frame_size < 0 || att_mtu < 0 || att_mtu > 0xFFFF) {
        return 0;
    }
    return (int32_t)mcl_ble_fragment_count((size_t)frame_size, (uint16_t)att_mtu);
}

MCLX_API int32_t mclx_fragment(const uint8_t *frame, int32_t frame_size,
                               int32_t att_mtu, int32_t index,
                               uint8_t *out, int32_t out_capacity)
{
    size_t written = 0u;
    mcl_ble_status_t st;

    if (frame == NULL || out == NULL || frame_size < 0 || index < 0 ||
        att_mtu < 0 || att_mtu > 0xFFFF || out_capacity < 0) {
        return MCLX_FAIL(MCL_BLE_ERR_INVALID_ARGUMENT);
    }

    st = mcl_ble_fragment(frame, (size_t)frame_size, (uint16_t)att_mtu,
                          (size_t)index, out, (size_t)out_capacity, &written);
    if (st != MCL_BLE_OK) {
        return MCLX_FAIL(st);
    }
    return (int32_t)written;
}

/* Reassembly state stays caller-owned; the caller allocates this many bytes. */
MCLX_API int32_t mclx_reassembler_size(void)
{
    return (int32_t)sizeof(mcl_ble_reassembler_t);
}

MCLX_API void mclx_reassembler_reset(void *state)
{
    mcl_ble_reassembler_reset((mcl_ble_reassembler_t *)state);
}

/*
 * Feed one fragment.
 *
 * Returns the frame length and copies it to `out` when a frame completes, 0
 * when more fragments are needed, or a negative status on a refusal. The
 * incomplete case is deliberately not an error: it is the normal path.
 */
MCLX_API int32_t mclx_reassemble(void *state,
                                 const uint8_t *fragment, int32_t fragment_size,
                                 uint8_t *out, int32_t out_capacity)
{
    mcl_ble_reassembler_t *r = (mcl_ble_reassembler_t *)state;
    size_t frame_size = 0u;
    mcl_ble_status_t st;
    size_t i;

    if (r == NULL || fragment == NULL || out == NULL ||
        fragment_size < 0 || out_capacity < 0) {
        return MCLX_FAIL(MCL_BLE_ERR_INVALID_ARGUMENT);
    }

    st = mcl_ble_reassemble(r, fragment, (size_t)fragment_size, &frame_size);
    if (st == MCL_BLE_ERR_INCOMPLETE) {
        return 0;
    }
    if (st != MCL_BLE_OK) {
        return MCLX_FAIL(st);
    }
    if (frame_size > (size_t)out_capacity) {
        return MCLX_FAIL(MCL_BLE_ERR_RANGE);
    }

    for (i = 0u; i < frame_size; ++i) {
        out[i] = r->buffer[i];
    }
    return (int32_t)frame_size;
}

/*
 * Decode a Link frame and, when it carries semantics, the object inside it.
 *
 * Reports the exact refusal rather than a generic failure, so a harness can
 * assert that a peer refused for the reason the specification requires and not
 * merely that it refused. `out_kind` is -1 when the frame carries no object.
 */
MCLX_API int32_t mclx_decode_frame(const uint8_t *frame, int32_t frame_size,
                                   int32_t *out_class, uint32_t *out_source_ref,
                                   int32_t *out_sequence, int32_t *out_kind)
{
    mcl_link_frame_t f;
    size_t consumed = 0u;
    mcl_link_status_t lst;

    if (frame == NULL || frame_size < 0 || out_class == NULL ||
        out_source_ref == NULL || out_sequence == NULL || out_kind == NULL) {
        return MCLX_FAIL(MCL_LINK_ERR_INVALID_ARGUMENT);
    }

    lst = mcl_link_frame_decode(frame, (size_t)frame_size, &f, &consumed);
    if (lst != MCL_LINK_OK) {
        return MCLX_FAIL(lst);
    }
    if (consumed != (size_t)frame_size) {
        /* An exact boundary was declared and not met. */
        return MCLX_FAIL(MCL_LINK_ERR_RANGE);
    }

    *out_class = (int32_t)f.frame_class;
    *out_source_ref = f.source_ref;
    *out_sequence = (int32_t)f.sequence;
    *out_kind = -1;

    if (f.payload_len > 0u &&
        (f.frame_class == MCL_LINK_CLASS_CONTACT ||
         f.frame_class == MCL_LINK_CLASS_DATA)) {
        mcl_wire_tier0_t obj;
        size_t wire_consumed = 0u;
        mcl_wire_status_t wst = mcl_wire_tier0_decode(f.payload,
                                                      (size_t)f.payload_len,
                                                      &obj, &wire_consumed);
        if (wst != MCL_WIRE_OK) {
            return MCLX_FAIL(wst);
        }
        if (wire_consumed != (size_t)f.payload_len) {
            return MCLX_FAIL(MCL_WIRE_ERR_NONCANONICAL);
        }
        *out_kind = (int32_t)obj.kind;
    }

    return 0;
}
