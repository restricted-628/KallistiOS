#ifndef TEST_PLAYER_MOCK_H
#define TEST_PLAYER_MOCK_H
#include <dc/sound/stream.h>
#include <stdbool.h>
enum mock_failure { MOCK_OK, MOCK_CREATE, MOCK_ADD, MOCK_EXEC_START, MOCK_ALLOC,
    MOCK_SOUND_START, MOCK_POLL, MOCK_UPDATE, MOCK_STOP, MOCK_JOIN, MOCK_CALLBACK };
extern enum mock_failure mock_failure;
extern snd_stream_callback_t mock_callback;
extern void *mock_userdata;
extern snd_stream_config_t mock_output;
extern uint32_t mock_update_fields;
extern unsigned mock_allocs,mock_starts,mock_stops,mock_joins,mock_updates;
extern size_t mock_frames;
extern int mock_async_error;
extern bool mock_status_failure;
extern int16_t mock_pcm[65536];
void mock_reset(void);
void mock_pause(void);
#endif
