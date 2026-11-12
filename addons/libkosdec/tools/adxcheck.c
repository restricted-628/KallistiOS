/* Original read-only asset checker. Uses the existing validation math model. */
#include <kosdec/adx.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] * 256u + p[1]; }
static uint32_t be32(const uint8_t *p) { return be16(p) * 65536u + be16(p + 2); }

/* Byte-preserving JSON: a path byte >=128 is represented by U+0080..U+00FF.
   Consumers can recover the original OS path by encoding path_bytes as Latin-1. */
static void quoted(const char *s) {
    putchar('"');
    for(const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if(*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if(*p < 32 || *p >= 127) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static const char *status_name(enum kosdec_adx_status status) {
    switch(status) {
        case KOSDEC_ADX_DONE: return "compatible";
        case KOSDEC_ADX_INVALID: return "invalid";
        case KOSDEC_ADX_UNSUPPORTED: return "unsupported";
        case KOSDEC_ADX_TRUNCATED: return "truncated";
        case KOSDEC_ADX_IO_ERROR: return "io_error";
        default: return "checker_error";
    }
}

static int check(const char *path) {
    struct kosdec_adx_decoder decoder;
    kosdec_adx_init(&decoder);
    uint8_t input[4096], declared[20];
    int16_t pcm[32 * 2];
    size_t available = 0, offset = 0, declared_bytes = 0;
    uint64_t consumed = 0;
    bool eof = false;
    int io_error = 0;
    enum kosdec_adx_status status = KOSDEC_ADX_MORE;
    FILE *file = fopen(path, "rb");
    if(!file) { io_error = errno; status = KOSDEC_ADX_IO_ERROR; }
    while(file && status < KOSDEC_ADX_DONE) {
        if(offset == available && !eof) {
            errno = 0;
            available = fread(input, 1, sizeof(input), file);
            offset = 0;
            if(declared_bytes < sizeof(declared)) {
                size_t n = sizeof(declared) - declared_bytes;
                if(n > available) n = available;
                memcpy(declared + declared_bytes, input, n);
                declared_bytes += n;
            }
            if(ferror(file)) {
                io_error = errno ? errno : EIO;
                status = KOSDEC_ADX_IO_ERROR;
                break;
            }
            eof = feof(file) != 0;
        }
        struct kosdec_adx_result r = kosdec_adx_decode(&decoder,
            input + offset, available - offset, pcm, 32, eof);
        offset += r.consumed;
        consumed += r.consumed;
        status = r.status;
    }
    if(file && fclose(file) != 0) {
        io_error = errno ? errno : EIO;
        status = KOSDEC_ADX_IO_ERROR;
    }
    printf("{\"path_bytes\":"); quoted(path);
    printf(",\"status\":"); quoted(status_name(status));
    printf(",\"compatible\":%s,\"bytes_consumed\":%" PRIu64
           ",\"decoded_frames\":%" PRIu32 ",\"declared\":",
           status == KOSDEC_ADX_DONE ? "true" : "false", consumed, decoder.frames_done);
    if(declared_bytes == sizeof(declared)) {
        printf("{\"signature\":%" PRIu32 ",\"header_bytes\":%" PRIu32
               ",\"encoding\":%u,\"block_bytes\":%u,\"sample_bits\":%u,\"channels\":%u"
               ",\"sample_rate\":%" PRIu32 ",\"frames\":%" PRIu32
               ",\"cutoff\":%" PRIu32 ",\"version\":%u,\"flags\":%u}",
               be16(declared), be16(declared + 2) + 4u, declared[4], declared[5],
               declared[6], declared[7], be32(declared + 8), be32(declared + 12),
               be16(declared + 16), declared[18], declared[19]);
    }
    else printf("null");
    printf(",\"validated_header\":");
    struct kosdec_adx_info info;
    bool header_ready = kosdec_adx_get_info(&decoder, &info) == KOSDEC_ADX_DONE;
    if(header_ready) {
        printf("{\"sample_rate\":%" PRIu32 ",\"channels\":%u,\"frames\":%" PRIu32
               ",\"loop\":", info.sample_rate, info.channels, info.total_frames);
        if(info.loop.present)
            printf("{\"start_frame\":%" PRIu32 ",\"end_frame\":%" PRIu32
                   ",\"start_byte\":%" PRIu32 ",\"end_byte\":%" PRIu32 "}",
                   info.loop.start_frame, info.loop.end_frame,
                   info.loop.start_byte, info.loop.end_byte);
        else printf("null");
        printf("}");
    }
    else printf("null");
    printf(",\"default_player_rate_supported\":%s", !header_ready ? "null" :
           info.sample_rate >= 8000 && info.sample_rate <= 48000 ? "true" : "false");
    printf(",\"io_error\":");
    if(io_error) quoted(strerror(io_error)); else printf("null");
    puts("}");
    return status != KOSDEC_ADX_DONE;
}

int main(int argc, char **argv) {
    if(argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: adxcheck [--] FILE...\n"
             "Read-only full-file validation against current libADX admission.\n"
             "One JSON record per file; declared fields are not validation.\n"
             "Profiles: encoding 3, v3/v5, mono/stereo, integer rates 1..48000 Hz.\n"
             "Cutoff: 0..floor(rate/2); default player output: 8000..48000 Hz.\n"
             "Exit: 0 all compatible; 1 any rejection/I/O error; 2 usage/output error.\n"
             "Host validation model only; no target audio or full CRI parity claim.");
        return fflush(stdout) != 0 || ferror(stdout) ? 2 : 0;
    }
    int first = argc > 1 && !strcmp(argv[1], "--") ? 2 : 1;
    if(first == argc) {
        fputs("Usage: adxcheck [--] FILE... (use --help for details)\n", stderr);
        return 2;
    }
    int rejected = 0;
    for(int i = first; i < argc; ++i) rejected |= check(argv[i]);
    if(fflush(stdout) != 0 || ferror(stdout)) return 2;
    return rejected;
}
