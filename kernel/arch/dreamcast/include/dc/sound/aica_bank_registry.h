/* Original experimental bank registry wire layout. Private, not a stable ABI. */
#ifndef __DC_SOUND_AICA_BANK_REGISTRY_H
#define __DC_SOUND_AICA_BANK_REGISTRY_H
#include <stdint.h>
#include <stddef.h>
#include <dc/sound/aica_completion.h>
#define AICA_DRIVER_FEATURE_BANK_REGISTRY 0x40u
#define AICA_BANK_LAYOUT_VERSION 1u
#define AICA_CMD_BANK_REGISTER 0x0bu
#define AICA_CMD_BANK_UNREGISTER 0x0cu
#define AICA_MEM_BANK_HEADER 0x027a00u
#define AICA_MEM_BANK_RECORDS 0x027b00u
#define AICA_MEM_BANK_END 0x029b00u
#define AICA_BANK_SLOTS 128u
#define AICA_BANK_BUDGET 32u
#define AICA_BANK_EMPTY 0u
#define AICA_BANK_VALIDATING 1u
#define AICA_BANK_ACTIVE 2u
#define AICA_BANK_PENDING 0u
#define AICA_BANK_OK 1u
#define AICA_BANK_BAD_IMAGE 2u
#define AICA_BANK_BUSY 3u
#define AICA_BANK_STALE 4u
#define AICA_BANK_EXHAUSTED 5u
#define AICA_BANK_PIN_QUEUED 0u
#define AICA_BANK_PIN_VOICE 1u
#define AICA_BANK_PIN_PRODUCER 2u
typedef struct aica_bank_header {
    uint32_t sequence, layout_version, ready, session, slot_count, reserved[11];
} aica_bank_header_t;
typedef struct aica_bank_record {
    uint32_t sequence, session, generation, cookie, operation, result, state;
    uint32_t image_base, image_bytes, sample_count, zone_count;
    uint32_t queued_refs, voice_refs, producer_refs, flags, reserved;
} aica_bank_record_t;
typedef struct aica_bank_register {
    uint32_t session, slot, expected_generation, image_base, image_bytes, version;
} aica_bank_register_t;
typedef struct aica_bank_unregister {
    uint32_t session, slot, generation;
} aica_bank_unregister_t;
#ifdef __cplusplus
#define AICA_B_ASSERT static_assert
#else
#define AICA_B_ASSERT _Static_assert
#endif
AICA_B_ASSERT(sizeof(aica_bank_header_t)==64, "bank header");
AICA_B_ASSERT(sizeof(aica_bank_record_t)==64, "bank record");
AICA_B_ASSERT(sizeof(aica_bank_register_t)==24, "bank register");
AICA_B_ASSERT(sizeof(aica_bank_unregister_t)==12, "bank unregister");
AICA_B_ASSERT(offsetof(aica_bank_record_t, image_base)==28, "bank image offset");
AICA_B_ASSERT(offsetof(aica_bank_record_t, queued_refs)==44, "bank references");
AICA_B_ASSERT(AICA_MEM_BANK_HEADER==AICA_MEM_COMPLETION_END, "bank follows completion");
AICA_B_ASSERT(AICA_MEM_BANK_RECORDS+128*64==AICA_MEM_BANK_END, "bank extent");
AICA_B_ASSERT(AICA_MEM_BANK_END<=0x30000, "bank below allocations");
#undef AICA_B_ASSERT
#endif
