/* Original incremental ADX decoder; no device or scheduler ownership. */
#ifndef KOSDEC_ADX_DECODER_H
#define KOSDEC_ADX_DECODER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define KOSDEC_ADX_MAX_FRAMES UINT32_C(2147483647)
#define KOSDEC_ADX_HEADER_BUDGET 512u

enum kosdec_adx_status {
    KOSDEC_ADX_MORE, KOSDEC_ADX_NEED_INPUT, KOSDEC_ADX_NEED_OUTPUT, KOSDEC_ADX_NEED_END,
    KOSDEC_ADX_DONE, KOSDEC_ADX_INVALID, KOSDEC_ADX_UNSUPPORTED, KOSDEC_ADX_TRUNCATED,
    KOSDEC_ADX_CANCELLED, KOSDEC_ADX_BAD_ARGUMENT, KOSDEC_ADX_IO_ERROR,
    KOSDEC_ADX_NOT_FOUND
};
/* Validated v3 loop metadata. The incremental codec always decodes once through
   the file; this information does not enable automatic looping. End is exclusive.
   Byte offsets are absolute from file start. end_byte follows the block group
   containing the last loop sample, and may include compressed sample padding. */
struct kosdec_adx_loop {
    bool present;
    uint32_t start_frame, end_frame, start_byte, end_byte;
};
struct kosdec_adx_decoder {
    uint32_t magic, total_frames, frames_done, clips;
    uint32_t header_used, header_bytes, sample_rate;
    uint16_t cutoff;
    uint8_t channels, version, block_used;
    uint32_t footer_used, footer_bytes;
    bool header_ready;
    enum kosdec_adx_status terminal;
    int16_t coefficient[2], history[2][2];
    struct kosdec_adx_loop loop;
    uint8_t header[20], extension[24], footer_length[2], block[36];
};
struct kosdec_adx_result {
    enum kosdec_adx_status status;
    size_t consumed, frames;
};
/* Immutable metadata snapshot, independent of the decoder's mutable counters.
   Counts are PCM frames (samples per channel); all byte offsets are absolute.
   inserted_frames describes leading PCM already encoded to align a loop. It is
   included in the encoded timeline, not a command to skip or synthesize samples. */
struct kosdec_adx_info {
    uint32_t header_bytes, total_frames, sample_rate;
    uint16_t cutoff, inserted_frames;
    uint8_t encoding, block_bytes, sample_bits, channels, version;
    struct kosdec_adx_loop loop;
};
/* DONE publishes validated header metadata, even after a later body error or
   cancellation. Before header validation: NEED_INPUT, or the terminal error.
   BAD_ARGUMENT for invalid state/output. Never mutates decoder; output remains
   unchanged unless DONE. Output must be disjoint from decoder storage.
   Serialize against decoding, including producer use. */
enum kosdec_adx_status kosdec_adx_get_info(const struct kosdec_adx_decoder *,
                                        struct kosdec_adx_info *);
/* Inspect a contiguous prefix without changing a live decoder or emitting PCM.
   DONE means the complete supported header/end record was validated, NOT that
   its body, file EOF or playback is valid. Bytes after that record are ignored.
   NEED_INPUT means supply a longer prefix; malformed/unsupported records use
   the same status/policy as decode. Output is unchanged unless DONE. NULL input
   is valid only with zero bytes; output must be non-NULL and disjoint from input.
   Input may be byte-aligned. No allocation/I/O/scheduler/FPU-state changes.
   Work is bounded by one record (at most65539 bytes), using512-byte parser steps;
   these whole-record probes are not the incremental real-time decode API.
   Endcode size is its declared validated extent, not the supplied buffer size.
   These strict admission queries differ from CRI's permissive recognizers. */
enum kosdec_adx_status kosdec_adx_probe_header(const uint8_t *, size_t,
                                            struct kosdec_adx_info *);
enum kosdec_adx_status kosdec_adx_probe_endcode(const uint8_t *, size_t,
                                             uint32_t *endcode_bytes);
/* Construct the v3 SFA prefix for headerless encoding3/block18/4-bit ADX.
   Fixed500Hz cutoff, no loop, zero extensions. Feed it before the body; this
   does not encode PCM, append an endcode or mutate an existing stream/player.
   DONE writes exactly KOSDEC_ADX_SFA_HEADER_BYTES; remaining storage is untouched.
   Invalid format parameters return UNSUPPORTED; short capacity returns
   NEED_OUTPUT. On either, destination is unchanged. NULL with zero capacity is
   a valid size check; NULL with nonzero capacity returns BAD_ARGUMENT.
   Destination may be byte-aligned. No allocation/I/O/scheduler/hardware access. */
#define KOSDEC_ADX_SFA_HEADER_BYTES 288u
enum kosdec_adx_status kosdec_adx_make_sfa_header(uint8_t *, size_t,
    unsigned channels, uint32_t sample_rate, uint32_t total_frames);
/* Initialize/reinitialize caller-owned state. No allocation or implicit I/O.
   Profiles: v3/v5, encoding3/block18/4-bit, mono/stereo; integer rate1..48000Hz,
   cutoff0..floor(rate/2) (above44.1k is an extension),
   26..65539-byte header and 18..65539-byte zero-padded end record.
   Extensions: zero padding, or v3 insertion0..31/count1/type1 loop with block-aligned
   start and consistent offsets. Other extensions/coefficient domains unsupported.
   Metadata (total_frames/header_bytes/sample_rate/cutoff/channels/version/loop)
   is valid only after header_ready. Fields remain implementation-owned. */
void kosdec_adx_init(struct kosdec_adx_decoder *decoder);
void kosdec_adx_cancel(struct kosdec_adx_decoder *decoder);
/* At most one logical group (32 PCM16 frames), or 512 header/footer bytes
   per call. Input can be arbitrarily fragmented. MORE may mean metadata budget
   exhausted. Output needs min(32, remaining)
   frames before any new group input is consumed. Final group's compressed
   padding is consumed, but its samples never advance output/history/time.
   Histories quantize at fixed logical group boundaries, independent of chunks.
   An accepted footer waits for final_input with no trailing bytes before DONE.
   Repeat final_input on calls draining its last span; NEED_OUTPUT is not EOF.
   DONE/errors/cancel are sticky. BAD_ARGUMENT does not mutate initialized state.
   Caller owns disjoint state/input/output lifetimes and output capacity
   (output_frames*channels int16_t), alignment, immutable input and serialized access.
   Decoder fields are inspectable but must not be changed between API calls.
   No callback, allocation, yield or hardware access occurs. */
struct kosdec_adx_result kosdec_adx_decode(struct kosdec_adx_decoder *decoder,
    const uint8_t *input, size_t input_bytes, int16_t *output,
    size_t output_frames, bool final_input);
#ifdef __cplusplus
}
#endif
#endif
