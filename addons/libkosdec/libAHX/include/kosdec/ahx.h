/* Original incremental framing for Dreamcast mono AHX. */
#ifndef KOSDEC_AHX_H
#define KOSDEC_AHX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOSDEC_AHX_MAX_HEADER_BYTES 512u
#define KOSDEC_AHX_FRAME_SAMPLES 1152u
#define KOSDEC_AHX_GRANULE_SAMPLES 96u

enum kosdec_ahx_result {
    KOSDEC_AHX_NEED_INPUT,
    KOSDEC_AHX_FRAME,
    KOSDEC_AHX_DONE,
    KOSDEC_AHX_INVALID,
    KOSDEC_AHX_UNSUPPORTED,
    KOSDEC_AHX_TRUNCATED,
    KOSDEC_AHX_BAD_ARGUMENT,
    KOSDEC_AHX_MORE,
    KOSDEC_AHX_NEED_OUTPUT,
    KOSDEC_AHX_FP_ERROR,
    KOSDEC_AHX_IO_ERROR
};

struct kosdec_ahx_info {
    uint32_t rate, declared_samples, header_bytes;
    /* Optional bytes between the fixed fields and (c)CRI. This view
       remains valid until the parser/decoder is reset or released. */
    const uint8_t *extension;
    uint16_t extension_bytes, extension_records;
};

struct kosdec_ahx_frame {
    const uint8_t *data;
    size_t bytes;
    uint32_t index;
    uint16_t valid_samples; /* Unpadded samples in this 1152-sample frame. */
};

enum kosdec_ahx_sync {
    KOSDEC_AHX_SYNC_EXHAUSTED,
    KOSDEC_AHX_SYNC_FRAME,
    KOSDEC_AHX_SYNC_END,
    KOSDEC_AHX_SYNC_BAD_ARGUMENT
};

/* Legacy reader resynchronization for a bounded buffer. Align the input
   bit cursor to its next byte, then scan by nibbles for the 12-bit sync
   word or the 32-bit AHXE lead code. On a match advance just past the
   marker, otherwise to the end of the supplied buffer. This identifies
   a marker; it does not validate a frame or change a decoder instance. */
enum kosdec_ahx_sync kosdec_ahx_scan_sync(const uint8_t *input, size_t bytes, size_t *bit_cursor);

struct kosdec_ahx_parser;
size_t kosdec_ahx_parser_workspace_size(void);
size_t kosdec_ahx_parser_workspace_alignment(void);

/* Initialize or reset an instance in aligned caller-owned storage.
   Callers serialize operations and keep inputs/outputs disjoint from it. */
struct kosdec_ahx_parser *kosdec_ahx_parser_init(void *workspace, size_t bytes);

/* Non-mutating. Copies metadata only after the complete CRI header is valid. */
bool kosdec_ahx_parser_info(const struct kosdec_ahx_parser *parser, struct kosdec_ahx_info *info);

/* Consume a prefix and return at most one structurally complete frame of
   unencrypted, nonempty mono AHX v6/type 0x10. FRAME returns a pointer into
   parser storage until the next feed or reset. Consume the indicated bytes
   before the next call. final_input says that no input follows this span.
   Reassert it on the rest of that span, even when zero bytes remain.
   DONE requires a matching frame count, AHXE footer and final_input.
   Only FRAME updates frame. Stream errors stick until reset. */
enum kosdec_ahx_result kosdec_ahx_parser_feed(struct kosdec_ahx_parser *parser,
    const uint8_t *input, size_t bytes, bool final_input,
    size_t *consumed, struct kosdec_ahx_frame *frame);

struct kosdec_ahx_decoder;
struct kosdec_ahx_decode_result {
    enum kosdec_ahx_result status;
    size_t consumed, samples, valid_samples;
};

size_t kosdec_ahx_decoder_workspace_size(void);
size_t kosdec_ahx_decoder_workspace_alignment(void);
struct kosdec_ahx_decoder *kosdec_ahx_decoder_init(void *workspace, size_t bytes);
bool kosdec_ahx_decoder_info(const struct kosdec_ahx_decoder *decoder, struct kosdec_ahx_info *info);

/* Same incremental input and reset rules as the parser. One call delivers
   at most 96 mono PCM16 samples including final coded padding. valid_samples
   counts the prefix covered by the declared duration. While draining PCM,
   consumed may be zero. MORE denotes output progress; DONE validates footer
   and EOF after the last PCM. Callers can offer any output capacity, including
   zero for input admission. All buffers must be disjoint. */
struct kosdec_ahx_decode_result kosdec_ahx_decode(struct kosdec_ahx_decoder *decoder,
    const uint8_t *input, size_t bytes, bool final_input, int16_t *output, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
