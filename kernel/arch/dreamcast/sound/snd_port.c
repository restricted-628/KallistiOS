/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <errno.h>
#include <kos/irq.h>
#include "snd_channel.h"
#include "snd_port_internal.h"

#define PORT_COUNT 64u
#define PORT_GENERATION_MAX (UINT32_MAX >> 6)

typedef struct port_slot {
    snd_port_info_t info;
    uint32_t generation, serial;
    unsigned priority, pending_priority;
    bool live, active, pending, attached;
} port_slot_t;

static port_slot_t ports[PORT_COUNT];

/* Caller holds IRQ exclusion for lookup and the entire software transition. */
static port_slot_t *lookup(snd_port_t handle) {
    port_slot_t *slot = &ports[handle & (PORT_COUNT - 1)];
    if(!slot->live || !handle || slot->generation != (handle >> 6)) {
        errno = EINVAL;
        return NULL;
    }
    return slot;
}

int snd_port_reserve(unsigned count, snd_port_t *handle) {
    int old, result = -1;
    bool exhausted = false;
    if(!handle || (count != 1 && count != 2)) {
        errno = EINVAL;
        return -1;
    }
    old = irq_disable();
    for(unsigned i = 0; i < PORT_COUNT; ++i) {
        port_slot_t *slot = &ports[i];
        int channels[2] = { -1, -1 };
        if(slot->live)
            continue;
        if(slot->generation == PORT_GENERATION_MAX) {
            exhausted = true;
            continue;
        }
        if(snd_channel_reserve(count, channels) < 0)
            goto done;
        ++slot->generation;
        slot->info.channel_count = count;
        slot->info.channels[0] = channels[0];
        slot->info.channels[1] = channels[1];
        slot->serial = 0;
        slot->priority = slot->pending_priority = 0;
        slot->active = slot->pending = slot->attached = false;
        slot->live = true;
        *handle = (slot->generation << 6) | i;
        result = 0;
        goto done;
    }
    errno = exhausted ? EOVERFLOW : ENOSPC;
done:
    irq_restore(old);
    return result;
}

int snd_port_query(snd_port_t handle, snd_port_info_t *info) {
    int old, result = -1;
    port_slot_t *slot;
    if(!info) {
        errno = EINVAL;
        return -1;
    }
    old = irq_disable();
    slot = lookup(handle);
    if(slot) {
        *info = slot->info;
        result = 0;
    }
    irq_restore(old);
    return result;
}

int snd_port_close(snd_port_t handle) {
    int old = irq_disable(), result = -1;
    port_slot_t *slot = lookup(handle);
    if(slot) {
        if(slot->pending || slot->attached)
            errno = EBUSY;
        else {
            snd_channel_release(slot->info.channel_count, slot->info.channels);
            slot->live = false;
            result = 0;
        }
    }
    irq_restore(old);
    return result;
}

int snd_port_begin(snd_port_t handle, unsigned priority,
                   snd_port_transaction_t *transaction) {
    int old, result = -1;
    port_slot_t *slot;
    if(!transaction || priority > 15) {
        errno = EINVAL;
        return -1;
    }
    old = irq_disable();
    slot = lookup(handle);
    if(!slot)
        goto done;
    if(slot->pending) {
        errno = EBUSY;
        goto done;
    }
    if(slot->active && priority && slot->priority && priority < slot->priority) {
        result = 0;
        goto done;
    }
    if(slot->serial == UINT32_MAX) {
        errno = EOVERFLOW;
        goto done;
    }
    ++slot->serial;
    slot->pending_priority = priority;
    slot->pending = true;
    transaction->port = handle;
    transaction->serial = slot->serial;
    result = 1;
done:
    irq_restore(old);
    return result;
}

int snd_port_finish(snd_port_transaction_t transaction, bool succeeded) {
    int old = irq_disable(), result = -1;
    port_slot_t *slot = lookup(transaction.port);
    if(slot) {
        if(!slot->pending || transaction.serial != slot->serial)
            errno = EINVAL;
        else {
            if(succeeded) {
                slot->priority = slot->pending_priority;
                slot->active = true;
            }
            slot->pending = false;
            result = 0;
        }
    }
    irq_restore(old);
    return result;
}

/* Attach and close share this short atomic generation-checked transition. */
int snd_port_playback_pin(snd_port_t handle, unsigned *channel) {
    int old = irq_disable(), result = -1;
    port_slot_t *slot = lookup(handle);
    if(slot) {
        if(slot->attached || slot->pending)
            errno = EBUSY;
        else if(slot->info.channel_count != 1)
            errno = ENOTSUP;
        else {
            slot->attached = true;
            *channel = (unsigned)slot->info.channels[0];
            result = 0;
        }
    }
    irq_restore(old);
    return result;
}

int snd_port_playback_validate(snd_port_t handle) {
    int old = irq_disable(), result = -1;
    port_slot_t *slot = lookup(handle);
    if(slot) {
        if(!slot->attached)
            errno = EINVAL;
        else
            result = 0;
    }
    irq_restore(old);
    return result;
}

int snd_port_playback_unpin(snd_port_t handle) {
    int old = irq_disable(), result = -1;
    port_slot_t *slot = lookup(handle);
    if(slot) {
        if(!slot->attached)
            errno = EINVAL;
        else if(slot->pending)
            errno = EBUSY;
        else {
            slot->attached = false;
            result = 0;
        }
    }
    irq_restore(old);
    return result;
}

int snd_port_playback_stopped(snd_port_t handle) {
    int old = irq_disable(), result = -1;
    port_slot_t *slot = lookup(handle);
    if(slot) {
        if(!slot->attached || slot->pending)
            errno = EINVAL;
        else {
            slot->active = false;
            slot->priority = 0;
            result = 0;
        }
    }
    irq_restore(old);
    return result;
}
