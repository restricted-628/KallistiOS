/* Original ABK draft-v1 parser/load planner. Private, not a published ABI. */
#ifndef ABK_H
#define ABK_H
#include <stdint.h>

#define ABK_MAX_SAMPLES 256u
#define ABK_MAX_ZONES 1024u
#define ABK_MAX_FILE 0x200000u
#define ABK_MAX_PAYLOAD 0x1d0000u

typedef enum {
    ABK_OK = 0, ABK_ARGUMENT, ABK_FORMAT, ABK_CRC, ABK_CAPACITY, ABK_ALIAS,
    ABK_DESTINATION
} abk_result_t;

typedef struct {
    uint32_t file_bytes, payload_file_offset, payload_bytes;
    uint32_t sample_count, zone_count;
} abk_plan_t;

typedef struct {
    uint32_t file_offset, payload_offset, bytes, frames, rate;
    uint32_t loop_start, loop_end, encoding, loop;
} abk_sample_t;

/* Values in frozen Python ZONE_FIELDS order; fine_cents is signed at index8. */
typedef struct {
    uint32_t sample;
    int32_t values[20];
} abk_zone_t;

typedef struct {
    uint32_t source_file_offset, sound_ram_offset, bytes;
} abk_placement_t;

/* data[0,size) must be readable immutable ordinary RAM for the entire call.
   Byte alignment is sufficient. size is exact file size, not buffer capacity.
   Output objects/arrays must be writable, naturally aligned, mutually disjoint
   and disjoint from input over their used ranges. Capacities are in records.
   On ANY failure all outputs are untouched. On success only required records
   and *plan are written. No allocation or I/O. Preserve input and plans without
   concurrent modification until all later consumers/transactions retire.
   Validate at resource admission, never in a refill/voice hot path. */
abk_result_t abk_plan_build(const void *data, uint32_t size, abk_plan_t *plan,
                          abk_sample_t *samples, uint32_t sample_capacity,
                          abk_zone_t *zones, uint32_t zone_capacity);

/* plan must be an unmodified successful plan. Caller MUST already own an
   exclusive contiguous sound-RAM reservation [base,base+reserved_bytes).
   This checks geometry only: it does not reserve memory or establish ownership.
   out must not overlap plan. Places only PCM payload; metadata remains host-side.
   No mutation on failure; no upload, publication or physical-lifetime claim. */
abk_result_t abk_plan_place(const abk_plan_t *plan, uint32_t base,
                          uint32_t reserved_bytes, abk_placement_t *out);

/* Next software transfer extent from an unmodified successful placement.
   committed is a32-aligned completed prefix; limit is32..32768, multiple32.
   out must be disjoint from placement. At end return OK with bytes0; caller
   must not submit that empty extent. Failure leaves out untouched. No I/O. */
abk_result_t abk_plan_chunk(const abk_placement_t *placement,
                          uint32_t committed, uint32_t limit,
                          abk_placement_t *out);
#endif
