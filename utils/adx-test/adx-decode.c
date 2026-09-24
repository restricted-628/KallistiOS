/* Synthetic differential-test driver, not a production file player. */
#include <dc/sound/adx.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if(argc != 3)
        return 2;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    if(!in || !out)
        return 2;
    snd_adx_decoder_t d;
    snd_adx_init(&d);
    uint8_t buffer[37];
    size_t pos = 0, size = 0;
    int16_t pcm[64];
    for(;;) {
        if(pos == size && !feof(in)) {
            size = fread(buffer, 1, sizeof(buffer), in);
            pos = 0;
            if(ferror(in))
                return 2;
        }
        snd_adx_result_t r = snd_adx_decode(&d, buffer + pos, size - pos,
                                            pcm, 32, feof(in) != 0);
        pos += r.consumed;
        for(size_t i = 0; i < r.frames * d.info.channels; ++i) {
            uint16_t v = (uint16_t)pcm[i];
            if(fputc(v & 255, out) == EOF || fputc(v >> 8, out) == EOF)
                return 2;
        }
        if(r.status == SND_ADX_DONE)
            break;
        if(r.status >= SND_ADX_INVALID || r.status == SND_ADX_NEED_OUTPUT)
            return 1;
    }
    int error = fclose(in);
    error |= fclose(out);
    return error ? 2 : 0;
}
