/* Opt-in AICA portion. LP and KEYONB qualify a sample boundary. The SH-4
   owner relies on terminal mixer serialization only on its Flycast profile. */
#include "flycast_pcm16_cut.h"

_Static_assert(FLYCAST_CUT_ADDR >= AICA_MEM_FINITE_STOPS_END &&
               FLYCAST_CUT_ADDR + sizeof(flycast_cut_mailbox_t) <= AICA_MEM_CHANNEL_STATUS,
               "Flycast cut mailbox range");

static volatile flycast_cut_mailbox_t *const flycast_cut =
    (volatile flycast_cut_mailbox_t *)FLYCAST_CUT_ADDR;
static flycast_cut_mailbox_t cut;
static uint32_t cut_last, cut_phase, cut_stopped, cut_ended;
enum { CUT_IDLE, CUT_QUOTED, CUT_ARMED };

static int flycast_cut_identity(void) {
    if(!cut.session || cut.session != observer.header->active_session ||
       cut.request != cut.slot[0].start || (cut.channels != 1 && cut.channels != 2) ||
       (cut.channels == 2 && (cut.slot[0].channel == cut.slot[1].channel ||
                              cut.slot[0].address == cut.slot[1].address)))
        return 0;
    for(unsigned i = 0; i < cut.channels; ++i) {
        const flycast_cut_slot_t *slot = &cut.slot[i];
        unsigned ch = slot->channel;
        if(ch >= 64 || !slot->start || !slot->command || aica_music_live_owned(music, ch) ||
           observer.completions[ch].session != cut.session ||
           observer.completions[ch].start_token != slot->start ||
           observer.completions[ch].command_token != slot->command ||
           observer.completions[ch].state != AICA_COMPLETION_INTERFERED ||
           observer.completions[ch].attribution_kind != AICA_OBSERVATION_ATTR_MANAGED)
            return 0;
        uint32_t control = CHNREG32(ch, 0);
        uint32_t address = ((control & 0x7fu) << 16) | (CHNREG32(ch, 4) & 0xffffu);
        if(address != slot->address || (control & 0x180u) ||
           (CHNREG32(ch, 8) & 0xffffu) || (CHNREG32(ch, 0x18) & 0x7fffu))
            return 0;
    }
    return 1;
}

static void flycast_cut_done(uint32_t result) {
    flycast_cut->result = result;
    flycast_cut->done = cut_last;
    cut_phase = CUT_IDLE;
}

static int flycast_cut_looping(unsigned ch) {
    return (CHNREG32(ch, 0) & 0x200u) &&
           (CHNREG32(ch, 0x0c) & 0xffffu) == FLYCAST_CUT_RING;
}

static int flycast_cut_has_lead(unsigned i) {
    unsigned ch = cut.slot[i].channel;
    uint32_t position = aica_get_pos(ch);
    return !aica_get_loop_end() && aica_is_playing(ch) &&
           position >= cut.position[i] && position + FLYCAST_CUT_LEAD < cut.cut;
}

/* Drop historical LP, then report CA while LP is still clear. Managed
   looping observations do not consume LP, so any subsequent loop before
   authorization invalidates this quote even if CA returns to the same value. */
static int flycast_cut_quote(void) {
    if(!flycast_cut_identity() || cut.cut <= FLYCAST_CUT_LEAD || cut.cut >= FLYCAST_CUT_RING)
        return 0;
    for(unsigned i = 0; i < cut.channels; ++i) {
        unsigned ch = cut.slot[i].channel;
        if(!flycast_cut_looping(ch))
            return 0;
        aica_get_pos(ch);
        (void)aica_get_loop_end();
        uint32_t position = aica_get_pos(ch);
        if(aica_get_loop_end() || !aica_is_playing(ch) || position + FLYCAST_CUT_LEAD >= cut.cut)
            return 0;
        flycast_cut->position[i] = cut.position[i] = position;
    }
    flycast_cut->quoted = cut_last;
    return 1;
}

static int flycast_cut_arm(void) {
    if(!flycast_cut_identity())
        return 0;
    for(unsigned i = 0; i < cut.channels; ++i) {
        if(!flycast_cut_looping(cut.slot[i].channel) || !flycast_cut_has_lead(i))
            return 0;
    }
    for(unsigned i = 0; i < cut.channels; ++i)
        CHNREG32(cut.slot[i].channel, 0x0c) = cut.cut;
    for(unsigned i = 0; i < cut.channels; ++i)
        CHNREG32(cut.slot[i].channel, 0) &= ~0x8200u;
    for(unsigned i = 0; i < cut.channels; ++i) {
        unsigned ch = cut.slot[i].channel;
        if((CHNREG32(ch, 0) & 0x8200u) ||
           (CHNREG32(ch, 0x0c) & 0xffffu) != cut.cut ||
           !flycast_cut_has_lead(i))
            return 0;
    }
    return 1;
}

static void flycast_cut_service(void) {
    if(cut_phase == CUT_QUOTED) {
        if(flycast_cut->authorize == cut_last) {
            if(flycast_cut_arm())
                cut_phase = CUT_ARMED;
            else
                flycast_cut_done(FLYCAST_CUT_FAILED);
        }
        else if(!flycast_cut_identity())
            flycast_cut_done(FLYCAST_CUT_FAILED);
        return;
    }
    if(cut_phase == CUT_ARMED) {
        if(!flycast_cut_identity()) {
            flycast_cut_done(FLYCAST_CUT_FAILED);
            return;
        }
        for(unsigned i = 0; i < cut.channels; ++i) {
            unsigned ch = cut.slot[i].channel;
            if((CHNREG32(ch, 0) & 0x200u) ||
               (CHNREG32(ch, 0x0c) & 0xffffu) != cut.cut) {
                flycast_cut_done(FLYCAST_CUT_FAILED);
                return;
            }
            uint32_t before = aica_get_pos(ch);
            if(aica_get_loop_end())
                cut_ended |= 1u << i;
            if(!aica_is_playing(ch) && (cut_ended & (1u << i))) {
                if(before && aica_get_pos(ch)) {
                    flycast_cut_done(FLYCAST_CUT_FAILED);
                    return;
                }
                cut_stopped |= 1u << i;
            }
        }
        if(cut_stopped == (cut.channels == 2 ? 3u : 1u))
            flycast_cut_done(FLYCAST_CUT_NATURAL);
        return;
    }

    uint32_t request = flycast_cut->request;
    if(request <= cut_last)
        return;
    cut = *flycast_cut;
    if(cut.request != request || flycast_cut->request != request)
        return;
    cut_last = request;
    cut_ended = cut_stopped = 0;
    if(flycast_cut_quote())
        cut_phase = CUT_QUOTED;
    else
        flycast_cut_done(FLYCAST_CUT_FAILED);
}
