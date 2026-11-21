#include <kosdec/adx_compat_service.h>
#include <errno.h>
#include <string.h>

static int fail(int error) {
    errno = error;
    return -1;
}

static bool valid(const struct kosdec_adx_compat_service *service) {
    return service && service->titles && service->config.clock_us &&
           service->capacity && service->capacity <= KOSDEC_ADX_COMPAT_MAX_TITLES;
}

static const struct kosdec_adx_compat_title *bound(
    const struct kosdec_adx_compat_service *service, unsigned slot) {
    if(!valid(service) || slot >= service->capacity || !service->titles[slot].ops.state)
        return NULL;
    return &service->titles[slot];
}

int kosdec_adx_compat_service_init(struct kosdec_adx_compat_service *service,
    struct kosdec_adx_compat_title *titles, unsigned capacity,
    const struct kosdec_adx_compat_config *config) {
    if(!service || !titles || !config || !config->clock_us ||
       !capacity || capacity > KOSDEC_ADX_COMPAT_MAX_TITLES)
        return fail(EINVAL);
    *service = (struct kosdec_adx_compat_service){
        .config = *config, .titles = titles, .capacity = capacity
    };
    memset(titles,0,capacity * sizeof(*titles));
    return 0;
}

int kosdec_adx_compat_enable_vsync_clock(struct kosdec_adx_compat_service *service) {
    if(!valid(service))
        return fail(EINVAL);
    if(service->servicing)
        return fail(EBUSY);
    for(unsigned i = 0; i < service->capacity; ++i)
        if(service->titles[i].ops.state)
            return fail(EBUSY);
    service->use_vsync_clock = true;
    return 0;
}

int kosdec_adx_compat_bind(struct kosdec_adx_compat_service *service, unsigned slot,
    const struct kosdec_adx_compat_title_ops *ops, void *context) {
    if(!valid(service) || slot >= service->capacity ||
       !ops || !ops->state || !ops->dispatch || !ops->observe)
        return fail(EINVAL);
    if(service->servicing)
        return fail(EBUSY);
    if(service->titles[slot].ops.state)
        return fail(EEXIST);
    service->titles[slot] = (struct kosdec_adx_compat_title){
        .ops = *ops, .context = context, .frequency = 60
    };
    return 0;
}

int kosdec_adx_compat_unbind(struct kosdec_adx_compat_service *service, unsigned slot) {
    if(!bound(service,slot))
        return fail(EINVAL);
    if(service->servicing)
        return fail(EBUSY);
    struct kosdec_adx_compat_title *title = &service->titles[slot];
    if(title->recovery)
        return fail(EBUSY);
    if(title->detach && title->detach(title->context) < 0)
        return -1;
    *title = (struct kosdec_adx_compat_title){0};
    return 0;
}

int kosdec_adx_compat_set_frequency(struct kosdec_adx_compat_service *service,
    unsigned slot, int32_t hz) {
    if(!bound(service,slot) || hz <= 0)
        return fail(EINVAL);
    if(service->servicing)
        return fail(EBUSY);
    service->titles[slot].frequency = (uint32_t)hz;
    return 0;
}

int kosdec_adx_compat_prefill(const struct kosdec_adx_compat_service *service,
    unsigned slot, uint32_t rate, uint32_t block_frames, bool has_loop,
    uint32_t *frames) {
    const struct kosdec_adx_compat_title *title = bound(service,slot);
    if(!title || !rate || !block_frames || !frames)
        return fail(EINVAL);
    uint64_t target = UINT64_C(3) * (rate / title->frequency);
    if(!has_loop)
        target /= 2;
    const uint64_t boundary = UINT64_C(2) * block_frames;
    target = (target / boundary + 1) * boundary;
    if(target > UINT32_MAX)
        return fail(EOVERFLOW);
    *frames = (uint32_t)target;
    return 0;
}

static int64_t signed_long(uint32_t raw) {
    return raw <= INT32_MAX ? raw : (int64_t)raw - (INT64_C(1) << 32);
}

static int32_t signed_short(uint16_t raw) {
    return raw <= INT16_MAX ? raw : (int32_t)raw - (INT32_C(1) << 16);
}

static void apply_observation(struct kosdec_adx_compat_title *title,
    enum kosdec_adx_compat_state state, const struct kosdec_adx_compat_observation *data) {
    const bool active = state >= KOSDEC_ADX_COMPAT_DECINFO && state <= KOSDEC_ADX_COMPAT_PLAYING &&
                        !data->decoder_done && !data->paused;
    const uint32_t playing_limit = 5u * title->frequency;
    if(active && state == KOSDEC_ADX_COMPAT_PLAYING) {
        title->stalled_checks = data->decoded_position == title->position ?
                                (uint16_t)(title->stalled_checks + 1u) : 0;
        if(data->decoded_position == title->position &&
           signed_short(title->stalled_checks) > signed_long(playing_limit)) {
            title->error = KOSDEC_ADX_COMPAT_STALLED;
            title->faults |= KOSDEC_ADX_COMPAT_FAULT_STALLED;
        }
        title->position = data->decoded_position;
    } else {
        title->stalled_checks = 0;
    }

    if(active && data->input_bytes < 64) {
        const uint32_t limit = state == KOSDEC_ADX_COMPAT_PLAYING ? playing_limit :
                               20u * title->frequency;
        ++title->low_checks;
        if(signed_short(title->low_checks) > signed_long(limit)) {
            title->error = KOSDEC_ADX_COMPAT_LOW_DATA;
            title->faults |= KOSDEC_ADX_COMPAT_FAULT_LOW_DATA;
        }
    } else {
        title->low_checks = 0;
    }
}

static bool valid_state(enum kosdec_adx_compat_state state) {
    return (unsigned)state <= KOSDEC_ADX_COMPAT_PLAYEND;
}

static int service_title(struct kosdec_adx_compat_service *service, unsigned slot,
    struct kosdec_adx_compat_result *result) {
    struct kosdec_adx_compat_title *title = &service->titles[slot];
    if(title->recover) {
        int recovery = title->recover(title->recovery,false);
        if(recovery)
            return recovery < 0 ? -1 : 0;
    }
    enum kosdec_adx_compat_state state = title->ops.state(title->context);
    if(!valid_state(state))
        return fail(EPROTO);
    int dispatch_error = 0;
    if(state != KOSDEC_ADX_COMPAT_STOP && title->ops.dispatch(title->context,state) < 0)
        dispatch_error = errno ? errno : EIO;
    const bool transport_fault = title->ops.transport_failed &&
                                 title->ops.transport_failed(title->context);
    if(dispatch_error && !transport_fault)
        return fail(dispatch_error);

    state = title->ops.state(title->context);
    if(!valid_state(state))
        return fail(EPROTO);
    const uint64_t now = service->config.clock_us(service->config.clock_context);
    if(now < service->last_check_us)
        return fail(EINVAL);
    const uint32_t cadence_us = (1000000u / title->frequency) * 8u / 10u;
    if(now - service->last_check_us < cadence_us)
        return dispatch_error ? fail(dispatch_error) : 0;
    service->last_check_us = now;
    result->watchdog_checks |= UINT32_C(1) << slot;
    title->faults = transport_fault ? KOSDEC_ADX_COMPAT_FAULT_TRANSPORT : 0;

    struct kosdec_adx_compat_observation data = {0};
    int observed = 0;
    if(state >= KOSDEC_ADX_COMPAT_DECINFO && state <= KOSDEC_ADX_COMPAT_PLAYING) {
        observed = title->ops.observe(title->context,&data);
        if(observed < 0)
            return dispatch_error ? fail(dispatch_error) : -1;
    }
    if(observed > 0)
        result->watchdog_deferred |= UINT32_C(1) << slot;
    else
        apply_observation(title,state,&data);
    if(title->recover && title->recover(title->recovery,true) < 0 && !dispatch_error)
        return -1;
    return dispatch_error ? fail(dispatch_error) : 0;
}

static void visit(struct kosdec_adx_compat_service *service, unsigned slot,
    struct kosdec_adx_compat_result *result) {
    if(!service->titles[slot].ops.state)
        return;
    const uint32_t bit = UINT32_C(1) << slot;
    result->visited |= bit;
    errno = 0;
    if(service_title(service,slot,result) < 0) {
        result->failed_titles |= bit;
        if(!result->first_error)
            result->first_error = errno ? errno : EIO;
    }
}

static void phase(struct kosdec_adx_compat_service *service, enum kosdec_adx_compat_phase id,
    struct kosdec_adx_compat_result *result) {
    const struct kosdec_adx_compat_phase_op *op = &service->config.phases[id];
    const uint32_t bit = UINT32_C(1) << id;
    bool serviced = false;
    int error = 0;
    for(unsigned i = 0; i < service->capacity; ++i) {
        const struct kosdec_adx_compat_title *title = &service->titles[i];
        if(!title->service_phase)
            continue;
        errno = 0;
        int status = title->service_phase(title->context,id);
        if(status < 0 && !error)
            error = errno ? errno : EIO;
        serviced |= status > 0;
    }
    if(op->service) {
        errno = 0;
        if(op->service(op->context) < 0 && !error)
            error = errno ? errno : EIO;
        serviced = true;
    }
    if(error) {
        result->failed_phases |= bit;
        if(!result->first_error)
            result->first_error = error;
    } else if(serviced) {
        result->serviced_phases |= bit;
    }
}

static int finish_pass(struct kosdec_adx_compat_service *service,
    struct kosdec_adx_compat_result *out, const struct kosdec_adx_compat_result *result,
    int original_errno) {
    service->servicing = false;
    *out = *result;
    errno = result->first_error ? result->first_error : original_errno;
    return result->first_error ? -1 : 0;
}

static int exec_server(struct kosdec_adx_compat_service *service,
    struct kosdec_adx_compat_result *out, bool vsync) {
    if(!valid(service) || !out)
        return fail(EINVAL);
    if(service->servicing)
        return fail(EBUSY);
    const int original_errno = errno;
    struct kosdec_adx_compat_result result = {0};
    service->servicing = true;
    if(vsync)
        ++service->vsync_count;
    for(unsigned i = KOSDEC_ADX_COMPAT_DECODER; i < KOSDEC_ADX_COMPAT_RELOAD; ++i)
        phase(service,(enum kosdec_adx_compat_phase)i,&result);
    for(unsigned i = 0; i < service->capacity; ++i)
        visit(service,i,&result);
    phase(service,KOSDEC_ADX_COMPAT_RELOAD,&result);
    return finish_pass(service,out,&result,original_errno);
}

int kosdec_adx_compat_exec_server(struct kosdec_adx_compat_service *service,
    struct kosdec_adx_compat_result *out) {
    return exec_server(service,out,false);
}

int kosdec_adx_compat_exec_vsync(struct kosdec_adx_compat_service *service,
    struct kosdec_adx_compat_result *out) {
    return exec_server(service,out,true);
}

int kosdec_adx_compat_get_vsync_count(const struct kosdec_adx_compat_service *service, uint32_t *count) {
    if(!valid(service) || !count)
        return fail(EINVAL);
    *count = service->vsync_count;
    return 0;
}

int kosdec_adx_compat_exec_title(struct kosdec_adx_compat_service *service, unsigned slot,
    struct kosdec_adx_compat_result *out) {
    if(!bound(service,slot) || !out)
        return fail(EINVAL);
    if(service->servicing)
        return fail(EBUSY);
    const int original_errno = errno;
    struct kosdec_adx_compat_result result = {0};
    service->servicing = true;
    visit(service,slot,&result);
    return finish_pass(service,out,&result,original_errno);
}

int kosdec_adx_compat_get_error(const struct kosdec_adx_compat_service *service,
    unsigned slot, enum kosdec_adx_compat_error *error) {
    const struct kosdec_adx_compat_title *title = bound(service,slot);
    if(!title || !error)
        return fail(EINVAL);
    *error = title->error;
    return 0;
}

void media_adx_compat_reset_title_error(struct kosdec_adx_compat_title *title) {
    title->error = KOSDEC_ADX_COMPAT_NO_ERROR;
    title->faults = 0;
    title->position = 0;
    title->low_checks = 0;
    title->stalled_checks = 0;
}

int kosdec_adx_compat_clear_error(struct kosdec_adx_compat_service *service, unsigned slot) {
    if(!bound(service,slot))
        return fail(EINVAL);
    if(service->servicing)
        return fail(EBUSY);
    media_adx_compat_reset_title_error(&service->titles[slot]);
    return 0;
}

int kosdec_adx_compat_get_faults(const struct kosdec_adx_compat_service *service,
    unsigned slot, uint32_t *faults) {
    const struct kosdec_adx_compat_title *title = bound(service,slot);
    if(!title || !faults)
        return fail(EINVAL);
    *faults = title->faults;
    return 0;
}
