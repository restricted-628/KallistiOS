/* Execute production register sequencing with injected MMIO storage. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "aica_cmd_iface.h"
#include "aica.h"
static uint32_t registers[0x3000 / 4], pending[64], lp_value;
static unsigned lp_reads, checks;
static aica_channel_t channels[64];
volatile aica_channel_t *chans = channels;
void arm_fiq_enable(void) {}
#define CHECK(x) do { ++checks; assert(x); } while(0)
static volatile uint32_t *reg32(unsigned offset) {
    assert(offset < sizeof(registers) && !(offset & 3));
    if(offset == 0x2810) {
        unsigned selected = ((uint8_t *)registers)[0x280d];
        assert(selected < 64);
        /* The destructive read must occur after key-off, before new key-on. */
        CHECK(!(registers[selected * 0x80 / 4] & AICA_CHANNEL_KEYONB));
        lp_value = pending[selected] << 15;
        pending[selected] = 0;
        ++lp_reads;
        return &lp_value;
    }
    return &registers[offset / 4];
}
#undef SNDREG32A
#undef SNDREG8A
#define SNDREG32A(x) reg32(x)
#define SNDREG8A(x) ((volatile uint8_t *)registers + (x))
#include "../../../kernel/arch/dreamcast/sound/arm/aica.c"
int main(void) {
    aica_channel_config_t config = {0};
    config.base = 0x30000; config.length = config.loopend = 32;
    config.freq = 44100; config.attack_rate = 31;
    config.direct_level = 15;
    const unsigned selected[] = {0, 63, 0, 63};
    for(unsigned i = 0; i < 4; ++i) {
        unsigned ch = selected[i], other = 63 - ch;
        pending[ch] = pending[other] = 1;
        registers[ch * 0x80 / 4] = AICA_CHANNEL_KEYONB;
        unsigned before = lp_reads;
        aica_channel_start(ch, &config, 0);
        CHECK(lp_reads == before + 1 && !pending[ch] && pending[other]);
        CHECK(((uint8_t *)registers)[0x280d] == ch);
        CHECK(registers[ch * 0x80 / 4] & AICA_CHANNEL_KEYONB);
        CHECK(registers[(ch * 0x80 + 0x0c) / 4] == config.loopend);
    }
    for(unsigned mode = 0; mode < 4; ++mode) {
        config.loop = mode == 1;
        config.type = mode < 2 ? AICA_SM_16BIT : mode - 1;
        pending[0] = 1;
        unsigned before = lp_reads;
        aica_channel_start(0, &config, mode == 0);
        CHECK(lp_reads == before && pending[0]);
    }
    printf("ARM register sequencing: %u checks passed\n", checks);
    return 0;
}
