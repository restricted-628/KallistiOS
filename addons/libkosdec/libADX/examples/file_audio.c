/* Original standalone BGM integration: caller supplies its own ADX asset.
   A port initializes global audio once and runs these owner operations in its
   audio thread. Do not initialize/shut down global sound for each music track. */
#include <kos.h>
#include <kosdec/adx_file.h>
#include <kosdec/adx_player.h>
#include <errno.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);
#define PCM_FRAMES 16384u
#define SOUND_BYTES 16384u
static alignas(32) uint8_t compressed[32768], producer_stack[16384];
static alignas(32) int16_t pcm[PCM_FRAMES * 2], scratch[SOUND_BYTES / 2 + 1];
static struct kosdec_adx_file file;
static struct kosdec_adx_sequence sequence;
static struct kosdec_adx_player player;

static void retained(const char *reason) {
    printf("ADX MUSIC: %s; sound allocations retained, reset to exit\n", reason);
    for(;;) thd_sleep(1000);
}

int main(int argc, char **argv) {
    const char *path = "/cd/test.adx";
    bool embedded = false, repeat = false;
    if(argc > 3 || (argc == 3 && strcmp(argv[1], "--loop") && strcmp(argv[1], "--repeat"))) {
        printf("Usage: file_audio.elf [--loop|--repeat] [path.adx]\n");
        return 1;
    }
    if(argc == 3) {
        embedded = !strcmp(argv[1], "--loop");
        repeat = !strcmp(argv[1], "--repeat");
        path = argv[2];
    }
    else if(argc == 2) {
        embedded = !strcmp(argv[1], "--loop");
        repeat = !strcmp(argv[1], "--repeat");
        if(!embedded && !repeat) path = argv[1];
    }
    file_t descriptor = fs_open(path, O_RDONLY);
    if(descriptor < 0) { perror(path); return 1; }
    enum kosdec_adx_status status = kosdec_adx_file_init_buffered(
        &file, descriptor, embedded, compressed, sizeof(compressed));
    if(status != KOSDEC_ADX_MORE) {
        printf("ADX MUSIC: file init status=%d errno=%d\n", status, file.io_errno);
        fs_close(descriptor); return 1;
    }
    if(repeat) {
        struct kosdec_adx_source source = {
            file.stream.read_at, file.stream.context, file.stream.input_bytes, 0
        };
        kosdec_adx_sequence_init(&sequence, true);
        status = kosdec_adx_sequence_enqueue(&sequence, &source);
        if(status == KOSDEC_ADX_MORE) status = kosdec_adx_sequence_start(&sequence);
        if(status != KOSDEC_ADX_MORE) { fs_close(descriptor); return 1; }
    }
    /* Only this standalone application owns global stream initialization. */
    if(snd_stream_init_ex(2, SOUND_BYTES) < 0) { fs_close(descriptor); return 1; }
    const struct kosdec_adx_player_config config = {
        .pcm = pcm, .pcm_samples = PCM_FRAMES * 2, .capacity = PCM_FRAMES,
        .scratch = scratch, .scratch_samples = SOUND_BYTES / 2 + 1,
        .stack = producer_stack, .stack_bytes = sizeof(producer_stack),
        .sound_bytes = SOUND_BYTES, .transfer_timeout_ms = 1000,
        .prefill_timeout_ms = 10000
    };
    if(kosdec_adx_player_init(&player, repeat ? NULL : &file.stream,
                            repeat ? &sequence : NULL, &config) < 0) {
        fs_close(descriptor); retained("player init failed");
    }
    int error = 0;
    bool pending = false, released = false;
    uint32_t previous = 0;
    uint64_t next_report = 0;
    if(kosdec_adx_player_start(&player) < 0) { error = errno; goto stop; }
    printf("ADX MUSIC: %s, A releases repeat/loop; START stops\n", path);
    for(;;) {
        if(kosdec_adx_player_poll(&player) < 0) { error = errno; break; }
        struct kosdec_adx_player_info info;
        if(kosdec_adx_player_get_info(&player, &info) < 0) { error = errno; break; }
        maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *state = controller ? maple_dev_status(controller) : NULL;
        uint32_t buttons = state ? state->buttons : 0;
        uint32_t pressed = buttons & ~previous;
        previous = buttons;
        if(pressed & CONT_START) break;
        if((pressed & CONT_A) && (embedded || repeat) && !released && !pending) {
            struct kosdec_adx_command command = {
                .kind = repeat ? KOSDEC_ADX_SEQUENCE_REPEAT : KOSDEC_ADX_RELEASE_LOOP,
                .enabled = false
            };
            status = kosdec_adx_player_post(&player, &command);
            if(status == KOSDEC_ADX_MORE) pending = true;
            else if(status != KOSDEC_ADX_DONE) { error = EIO; break; }
        }
        if(pending && kosdec_adx_player_take(&player, &status)) {
            pending = false;
            if(status != KOSDEC_ADX_MORE && status != KOSDEC_ADX_DONE) { error = EIO; break; }
        }
        if(info.state == KOSDEC_ADX_PLAYER_SOURCE_END && !released) {
            if(kosdec_adx_player_release_source(&player) < 0) { error = errno; break; }
            /* This synchronous provider has no remaining transfer after join. */
            fs_close(descriptor); descriptor = -1; released = true; pending = false;
            printf("ADX MUSIC: source joined; output tail then silence, START stops\n");
        }
        uint64_t now = timer_ms_gettime64();
        if(now >= next_report) {
            printf("ADX MUSIC: rate=%" PRIu32 " channels=%u copied=%" PRIu64
                   " starvations=%" PRIu64 " state=%d decoder=%d\n",
                   info.sample_rate, info.channels, info.copied_frames,
                   info.starvations, info.state, info.decoder);
            next_report = now + 1000;
        }
        thd_sleep(2);
    }
stop:
    /* Never close/free source or stack if joining the producer fails. */
    if(kosdec_adx_player_stop(&player, 2000) < 0) retained("stop/join failed");
    if(descriptor >= 0) fs_close(descriptor);
    struct kosdec_adx_player_info info;
    kosdec_adx_player_get_info(&player, &info);
    printf("ADX MUSIC: software stopped error=%d first_error=%d decoder=%d retained=%d\n",
           error, info.first_error, info.decoder, info.retained_sound);
    /* No stop-time delay can establish final sample-fetch retirement. */
    retained(error || info.first_error ? "playback failed" : "stopped");
}
