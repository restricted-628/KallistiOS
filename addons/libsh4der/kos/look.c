/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_look.h>
#include <errno.h>
#include <math.h>
int sh4der_kos_look_prepare(sh4der_kos_look_plan_t *out,const sh4der_look_params_t *params) {
    if(!out){errno=EINVAL;return -1;}
    sh4der_look_params_t p;
    if(params)p=*params;else sh4der_look_params_default(&p);
    if(!isfinite(p.light_s) || p.light_s<0 || p.light_s>3600 || !isfinite(p.dark_s) || p.dark_s<0 || p.dark_s>3600 ||
       !isfinite(p.transient) || p.transient<0 || p.transient>4 || !isfinite(p.open) || p.open<0 || p.open>4 ||
       !isfinite(p.gain_lo) || !isfinite(p.gain_hi) || p.gain_lo<.4f || p.gain_hi>2 || p.gain_lo>p.gain_hi) {errno=EDOM;return -1;}
    sh4der_look_bins(&out->bins);out->params=p;return 0;
}
int sh4der_kos_look_reset(sh4der_kos_look_state_t *s,uint64_t generation) {
    if(!s || !generation){errno=EINVAL;return -1;}
    sh4der_look_eye_reset(&s->eye);s->last=(sh4der_kos_frame_id_t){generation,0};return 0;
}
int sh4der_kos_look_update(const sh4der_kos_look_plan_t *p,sh4der_kos_look_state_t *s,
    const uint16_t *grid,size_t count,const sh4der_kos_frame_history_t *history,
    sh4der_kos_frame_id_t source,float dt) {
    if(!p || !s || !grid || !history){errno=EINVAL;return -1;}
    if(count<SH4DER_KOS_FRAME_SAMPLES){errno=ENOSPC;return -1;}
    if(!source.generation || source.generation!=s->last.generation || !source.render || source.render<=s->last.render){errno=ESTALE;return -1;}
    if(!isfinite(dt) || dt<0 || dt>60){errno=EDOM;return -1;}
    sh4der_kos_frame_metadata_t m;
    if(sh4der_kos_frame_history_get(history,source,&m)<0)return -1;
    if(m.gain_ln < -8 || m.gain_ln > 8){errno=EDOM;return -1;}
    uint16_t histogram[SH4DER_LOOK_BINS]={0};
    for(unsigned i=0;i<SH4DER_KOS_FRAME_SAMPLES;++i)++histogram[sh4der_look_luma565(grid[i])];
    sh4der_look_eye_t next=s->eye;
    next.gain_ln=m.gain_ln;
    sh4der_look_eye_step(&next,&p->params,sh4der_look_log_average(&p->bins,histogram,SH4DER_KOS_FRAME_SAMPLES),dt);
    s->eye=next;s->last=source;return 0;
}
