#include <kosdec/ahx.h>
#include "frame.h"

#include <stdalign.h>
#include <string.h>

/* ISO/IEC 13818-3 Annex B uses 4 x four-bit, 7 x three-bit and
   19 x two-bit allocation fields. Each active band can select up to
   three scale factors and carries 12 groups of three samples. */
enum {
    FILE_FIELDS_BYTES = 20, FILE_TAG_BYTES = 6,
    FRAME_HEADER_BYTES = 4,
    FRAME_MAX_BYTES = (AHX_ALLOCATION_END + AHX_BANDS * 20 +
                       12 * (4 * 42 + 7 * 21 + 19 * 10) + 7) / 8
};

enum stage {
    FILE_FIELDS, FILE_EXTENSION, FILE_TAG,
    FRAME_HEADER, FRAME_ALLOCATION, FRAME_SELECTION, FRAME_BODY, FOOTER, EOF_WAIT
};

struct kosdec_ahx_parser {
    struct kosdec_ahx_info info;
    enum stage stage;
    enum kosdec_ahx_result terminal;
    size_t have, need;
    uint32_t frames, samples;
    unsigned active_bands, group_bits;
    bool header_ready, footer_started;
    uint8_t buffer[FRAME_MAX_BYTES];
    uint8_t extension[KOSDEC_AHX_MAX_HEADER_BYTES - FILE_FIELDS_BYTES - FILE_TAG_BYTES];
};

static const uint8_t footer[] = {
    0x80, 0x01, 0x00, 0x0c, 'A', 'H', 'X', 'E', '(', 'c', ')', 'C', 'R', 'I', 0, 0
};

enum kosdec_ahx_sync kosdec_ahx_scan_sync(const uint8_t *input, size_t bytes, size_t *bit_cursor) {
    if(!bit_cursor || (!input && bytes) || bytes > SIZE_MAX / 8 || *bit_cursor > bytes * 8)
        return KOSDEC_AHX_SYNC_BAD_ARGUMENT;
    size_t end = bytes * 8;
    uint32_t bits = 0;
    size_t start = (*bit_cursor + 7) & ~(size_t)7;
    for(size_t at = start; at < end; at += 4) {
        unsigned shift = (at & 4) ? 0 : 4;
        bits = bits << 4 | (input[at / 8] >> shift & 15u);
        if((bits & 0xfffu) == 0xfffu) {
            *bit_cursor = at + 4;
            return KOSDEC_AHX_SYNC_FRAME;
        }
        if(bits == UINT32_C(0x8001000c)) {
            *bit_cursor = at + 4;
            return KOSDEC_AHX_SYNC_END;
        }
    }
    *bit_cursor = end;
    return KOSDEC_AHX_SYNC_EXHAUSTED;
}

size_t kosdec_ahx_parser_workspace_size(void) {
    return sizeof(struct kosdec_ahx_parser);
}

size_t kosdec_ahx_parser_workspace_alignment(void) {
    return alignof(struct kosdec_ahx_parser);
}

struct kosdec_ahx_parser *kosdec_ahx_parser_init(void *workspace, size_t bytes) {
    if(!workspace || bytes < sizeof(struct kosdec_ahx_parser) ||
       (uintptr_t)workspace % alignof(struct kosdec_ahx_parser))
        return NULL;
    struct kosdec_ahx_parser *p = workspace;
    *p = (struct kosdec_ahx_parser){ .need = FILE_FIELDS_BYTES };
    return p;
}

bool kosdec_ahx_parser_info(const struct kosdec_ahx_parser *p, struct kosdec_ahx_info *info) {
    if(!p || !info || !p->header_ready)
        return false;
    *info = p->info;
    return true;
}

static uint32_t be32(const uint8_t *b) {
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static enum kosdec_ahx_result file_fields(struct kosdec_ahx_parser *p) {
    const uint8_t *b = p->buffer;
    uint32_t header_bytes = ((uint32_t)b[2] << 8 | b[3]) + 4;
    if(b[0] != 0x80 || b[1] || header_bytes < FILE_FIELDS_BYTES + FILE_TAG_BYTES)
        return KOSDEC_AHX_INVALID;
    if(header_bytes > KOSDEC_AHX_MAX_HEADER_BYTES)
        return KOSDEC_AHX_UNSUPPORTED;
    if(b[4] != 0x10 || b[5] || b[6] || b[7] != 1 || b[18] != 6 || b[19])
        return KOSDEC_AHX_UNSUPPORTED;
    p->info = (struct kosdec_ahx_info) {
        .rate = be32(b + 8), .declared_samples = be32(b + 12), .header_bytes = header_bytes
    };
    if(!p->info.rate || !p->info.declared_samples)
        return KOSDEC_AHX_INVALID;
    p->need = header_bytes - FILE_FIELDS_BYTES - FILE_TAG_BYTES;
    if(p->need) {
        p->info.extension = p->extension;
        p->info.extension_bytes = (uint16_t)p->need;
    }
    p->stage = FILE_EXTENSION;
    p->have = 0;
    return KOSDEC_AHX_NEED_INPUT;
}

static bool file_extension(struct kosdec_ahx_parser *p) {
    /* The optional container extension starts with four bytes. Its last two
       encode the count of following 20-byte records. Keep the optional
       records and any later channel metadata as unmodified bytes. */
    if(p->need >= 4) {
        unsigned records = (unsigned)p->extension[2] << 8 | p->extension[3];
        if(records > (p->need - 4) / 20)
            return false;
        p->info.extension_records = (uint16_t)records;
    }
    p->stage = FILE_TAG;
    p->have = 0;
    p->need = FILE_TAG_BYTES;
    return true;
}

static void frame_allocation(struct kosdec_ahx_parser *p) {
    p->active_bands = p->group_bits = 0;
    unsigned cursor = 32;
    for(unsigned band = 0; band < AHX_BANDS; ++band) {
        unsigned allocation = ahx_read_bits(p->buffer, &cursor, ahx_allocation_width(band));
        unsigned q = ahx_quantizer(band, allocation);
        p->active_bands += q != 0;
        p->group_bits += ahx_code_bits(q) * (ahx_grouped(q) ? 1 : 3);
    }
    p->stage = FRAME_SELECTION;
    p->need = (AHX_ALLOCATION_END + 2 * p->active_bands + 7) / 8;
}

static void frame_selection(struct kosdec_ahx_parser *p) {
    static const uint8_t scale_count[] = {3, 2, 1, 2};
    unsigned cursor = AHX_ALLOCATION_END;
    unsigned scales = 0;
    for(unsigned band = 0; band < p->active_bands; ++band)
        scales += scale_count[ahx_read_bits(p->buffer, &cursor, 2)];
    p->need = (cursor + scales * 6 + 12 * p->group_bits + 7) / 8;
    p->stage = FRAME_BODY;
}

static enum kosdec_ahx_result frame_ready(struct kosdec_ahx_parser *p, struct kosdec_ahx_frame *frame) {
    uint32_t remaining = p->info.declared_samples - p->samples;
    unsigned valid = remaining < KOSDEC_AHX_FRAME_SAMPLES ? remaining : KOSDEC_AHX_FRAME_SAMPLES;
    *frame = (struct kosdec_ahx_frame) {
        .data = p->buffer, .bytes = p->have, .index = p->frames++, .valid_samples = (uint16_t)valid
    };
    p->samples += valid;
    p->have = 0;
    if(p->samples == p->info.declared_samples) {
        p->stage = FOOTER;
        p->need = sizeof(footer);
    } else {
        p->stage = FRAME_HEADER;
        p->need = FRAME_HEADER_BYTES;
    }
    return KOSDEC_AHX_FRAME;
}

static size_t copy_input(struct kosdec_ahx_parser *p, const uint8_t *input, size_t bytes) {
    size_t count = p->need - p->have;
    if(count > bytes)
        count = bytes;
    if(count) {
        uint8_t *dst = p->stage == FILE_EXTENSION ? p->extension : p->buffer;
        memcpy(dst + p->have, input, count);
    }
    p->have += count;
    return count;
}

enum kosdec_ahx_result kosdec_ahx_parser_feed(struct kosdec_ahx_parser *p,
    const uint8_t *input, size_t bytes, bool final_input,
    size_t *consumed, struct kosdec_ahx_frame *frame) {
    if(!consumed)
        return KOSDEC_AHX_BAD_ARGUMENT;
    *consumed = 0;
    if(!p || !frame || (!input && bytes))
        return KOSDEC_AHX_BAD_ARGUMENT;
    if(p->terminal != KOSDEC_AHX_NEED_INPUT) {
        if(p->terminal == KOSDEC_AHX_DONE && bytes)
            p->terminal = KOSDEC_AHX_INVALID;
        return p->terminal;
    }

    for(;;) {
        size_t left = bytes - *consumed;
        if(p->stage == EOF_WAIT) {
            if(left)
                return p->terminal = KOSDEC_AHX_INVALID;
            return p->terminal = final_input ? KOSDEC_AHX_DONE : KOSDEC_AHX_NEED_INPUT;
        }
        if(p->stage == FOOTER && !p->footer_started && left) {
            p->footer_started = true;
            if(input[*consumed] == 0) {
                ++*consumed;
                --left;
            }
        }
        if(left)
            *consumed += copy_input(p, input + *consumed, left);
        if(p->have != p->need)
            break;

        switch(p->stage) {
        case FILE_FIELDS: {
            enum kosdec_ahx_result result = file_fields(p);
            if(result != KOSDEC_AHX_NEED_INPUT)
                return p->terminal = result;
            break;
        }
        case FILE_EXTENSION:
            if(!file_extension(p))
                return p->terminal = KOSDEC_AHX_INVALID;
            break;
        case FILE_TAG:
            if(memcmp(p->buffer, "(c)CRI", FILE_TAG_BYTES))
                return p->terminal = KOSDEC_AHX_INVALID;
            p->header_ready = true;
            p->stage = FRAME_HEADER;
            p->have = 0;
            p->need = FRAME_HEADER_BYTES;
            break;
        case FRAME_HEADER:
            if((be32(p->buffer) & ~UINT32_C(0x100)) != UINT32_C(0xfff5e0c0))
                return p->terminal = KOSDEC_AHX_INVALID;
            p->stage = FRAME_ALLOCATION;
            p->need = (AHX_ALLOCATION_END + 7) / 8;
            break;
        case FRAME_ALLOCATION:
            frame_allocation(p);
            break;
        case FRAME_SELECTION:
            frame_selection(p);
            break;
        case FRAME_BODY:
            return frame_ready(p, frame);
        case FOOTER:
            if(memcmp(p->buffer, footer, sizeof(footer)))
                return p->terminal = KOSDEC_AHX_INVALID;
            p->stage = EOF_WAIT;
            break;
        default:
            return p->terminal = KOSDEC_AHX_INVALID;
        }
    }
    return p->terminal = final_input ? KOSDEC_AHX_TRUNCATED : KOSDEC_AHX_NEED_INPUT;
}
