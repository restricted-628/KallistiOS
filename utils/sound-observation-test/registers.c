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

static void check_routing(aica_channel_config_t config) {
    const unsigned channel = 37, base = channel * 0x80;
    uint8_t *mmio = (uint8_t *)registers;
    uint8_t expected[sizeof(registers)];
    const uint8_t adjacent[] = {0x93, 0x52, 0xd1};
    const struct { uint8_t bus, send, direct, packed; } routes[] = {
        {7, 12, 3, 0xc7}, {0, 0, 0, 0x00}, {15, 15, 15, 0xff},
    };

    for(unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        config.effect_channel = routes[i].bus;
        config.effect_send = routes[i].send;
        config.direct_level = routes[i].direct;
        memcpy(mmio + base + 0x21, adjacent, sizeof(adjacent));
        aica_channel_start(channel, &config, 1);
        CHECK(mmio[base + 0x20] == routes[i].packed);
        CHECK(!memcmp(mmio + base + 0x21, adjacent, sizeof(adjacent)));
        CHECK(mmio[base + 0x25] == routes[i].direct);

        mmio[base + 0x20] = 0x5a;
        mmio[base + 0x25] = 0xa5;
        memcpy(expected, registers, sizeof(expected));
        expected[base + 0x20] = routes[i].packed;
        expected[base + 0x25] = routes[i].direct;
        aica_channel_update(channel, &config, AICA_CHANNEL_UPDATE_ROUTING);
        CHECK(!memcmp(registers, expected, sizeof(expected)));
    }

    config.effect_channel = config.effect_send = config.direct_level = 0;
    config.vol = 255;
    mmio[base + 0x29] = 0x5a;
    expected[base + 0x29] = 0;
    aica_channel_update(channel, &config, AICA_CHANNEL_UPDATE_VOLUME);
    CHECK(!memcmp(registers, expected, sizeof(expected)));
}

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
    check_routing(config);
    uint8_t expected[sizeof(registers)];
    memcpy(expected, registers, sizeof(expected));
    aica_channel_set_tl(0, 128);
    aica_channel_set_tl(63, 255);
    expected[0x29] = 128;
    expected[63 * 0x80 + 0x29] = 255;
    CHECK(!memcmp(registers, expected, sizeof(expected)));
    printf("ARM register sequencing: %u checks passed\n", checks);
    return 0;
}
