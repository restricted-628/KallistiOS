#include "aica_sequence.h"
#include <stddef.h>
#include <limits.h>
static void clear(void *p,unsigned bytes) { uint8_t *b=p;while(bytes--) *b++=0; }
int aica_seq_validate_begin(aica_seq_validation_t *v,const aica_seq_program_t *p) {
    aica_seq_program_t copy;
    if(!v) return -1;
    if(!p) { clear(v,sizeof(*v));v->failed=1;return -1; }
    copy=*p;clear(v,sizeof(*v));v->failed=1;
    if(!copy.events || !copy.count || copy.count>65536 ||
       !copy.ticks_per_quarter || copy.ticks_per_quarter>9600 ||
       !copy.tempo_us || copy.tempo_us>60000000) return -1;
    v->failed=0;v->program=copy;return 0;
}
int aica_seq_validate_step(aica_seq_validation_t *v) {
    unsigned budget;
    if(!v || v->failed || !v->program.events || !v->program.count) return -1;
    if(v->ready) return 1;
    for(budget=0;budget<AICA_SEQ_BUDGET && v->index<v->program.count;budget++) {
        const aica_seq_event_t *e=&v->program.events[v->index];
        v->ticks+=e->delta;
        if(e->kind!=AICA_SEQ_MIDI && (e->status || e->a || e->b)) goto invalid;
        switch(e->kind) {
            case AICA_SEQ_MIDI:
                if(e->status<0x80 || e->status>=0xf0 || e->a>127 || e->b>127 ||
                   e->value || e->auxiliary) goto invalid;
                break;
            case AICA_SEQ_TEMPO:
                if(!e->value || e->value>60000000 || e->auxiliary) goto invalid;
                break;
            case AICA_SEQ_LOOP:
                if(v->depth==AICA_SEQ_DEPTH || e->value>65535 ||
                   e->auxiliary<=v->index || e->auxiliary>=v->program.count) goto invalid;
                v->begin[v->depth]=v->index;v->end[v->depth]=e->auxiliary;
                v->loop_ticks[v->depth]=v->ticks;++v->depth;break;
            case AICA_SEQ_LOOP_END:
                if(!v->depth || e->auxiliary || e->value!=v->begin[v->depth-1] ||
                   v->index!=v->end[v->depth-1] || v->ticks==v->loop_ticks[v->depth-1]) goto invalid;
                --v->depth;break;
            case AICA_SEQ_END:
                if(v->depth || v->index+1!=v->program.count || e->value || e->auxiliary) goto invalid;
                ++v->index;v->ready=1;return 1;
            default:goto invalid;
        }
        ++v->index;
    }
    if(v->index==v->program.count) goto invalid;
    return 0;
invalid:v->failed=1;return -1;
}
int aica_sequence_init(aica_sequence_t *s,const aica_seq_ops_t *ops) {
    if(!s || !ops || !ops->retain || !ops->release || !ops->emit ||
       !ops->silence || !ops->resume) return -1;
    clear(s,sizeof(*s));s->ops=*ops;s->speed_from=s->speed_target=4096;
    s->beat_shift=2;s->release_mode=1;return 0;
}
static int ready(const aica_seq_validation_t *v) { return v && v->ready && !v->failed; }
static void begin(aica_sequence_t *s,const aica_seq_program_t *p,unsigned priority) {
    s->program=*p;s->tempo=p->tempo_us;s->cursor=0;s->ticks=0;s->remaining=0;
    s->depth=s->loaded=s->counted=s->exit_loop=0;s->priority=(uint8_t)priority;
    s->state=AICA_SEQ_RUNNING;
}
void aica_sequence_stop(aica_sequence_t *s) {
    if(!s) return;
    if(s->state==AICA_SEQ_RUNNING || s->state==AICA_SEQ_PAUSED) {
        s->ops.silence(s->ops.context,s->release_mode,0);
        s->ops.release(s->ops.context,&s->program);
    }
    if(s->chain) { s->ops.release(s->ops.context,&s->chained);s->chain=0; }
    s->state=AICA_SEQ_IDLE;s->pending=s->remaining=0;
    s->depth=s->loaded=s->counted=s->exit_loop=0;
}
int aica_sequence_play(aica_sequence_t *s,const aica_seq_validation_t *v,unsigned priority) {
    if(!s || !ready(v) || priority>31 || s->state==AICA_SEQ_FAULT) return -1;
    if((s->state==AICA_SEQ_RUNNING || s->state==AICA_SEQ_PAUSED) &&
       priority && s->priority && priority<s->priority) return 0;
    if(s->ops.retain(s->ops.context,&v->program)<=0) return -2;
    aica_sequence_stop(s);begin(s,&v->program,priority);s->speed_remainder=0;return 1;
}
int aica_sequence_pause(aica_sequence_t *s) {
    if(!s || s->state!=AICA_SEQ_RUNNING) return -1;
    s->ops.silence(s->ops.context,s->release_mode,1);s->state=AICA_SEQ_PAUSED;return 0;
}
int aica_sequence_continue(aica_sequence_t *s) {
    int result;if(!s || s->state!=AICA_SEQ_PAUSED) return -1;
    result=s->ops.resume(s->ops.context);
    if(result>0) s->state=AICA_SEQ_RUNNING;
    else if(result<0) { aica_sequence_stop(s);s->state=AICA_SEQ_FAULT; }
    return result;
}
int aica_sequence_chain(aica_sequence_t *s,const aica_seq_validation_t *v) {
    if(!s || (s->state!=AICA_SEQ_RUNNING && s->state!=AICA_SEQ_PAUSED) || (v && !ready(v))) return -1;
    if(v && s->ops.retain(s->ops.context,&v->program)<=0) return -2;
    if(s->chain) s->ops.release(s->ops.context,&s->chained);
    s->chain=(uint8_t)(v!=NULL);if(v) s->chained=v->program;
    s->exit_loop=1;return 0;
}
static uint32_t speed(const aica_sequence_t *s) {
    if(!s->speed_duration) return s->speed_target;
    return (uint32_t)((int64_t)s->speed_from+
        ((int64_t)s->speed_target-s->speed_from)*s->speed_elapsed/s->speed_duration);
}
int aica_sequence_speed(aica_sequence_t *s,int relative,uint32_t ms) {
    if(!s || relative < -4032 || relative>8191 || ms>32767) return -1;
    s->speed_from=speed(s);s->speed_target=(uint32_t)(4096+relative);
    s->speed_elapsed=0;s->speed_duration=ms*1000;return 0;
}
int aica_sequence_beat(aica_sequence_t *s,unsigned multiply,unsigned shift) {
    if(!s || multiply>1 || shift>(multiply?3u:6u)) return -1;
    s->beat_multiply=(uint8_t)multiply;s->beat_shift=(uint8_t)shift;return 0;
}
uint64_t aica_sequence_ticks(const aica_sequence_t *s) {
    uint64_t partial;
    if(!s) return 0;
    if(!s->loaded || s->counted) return s->ticks;
    partial=(uint64_t)s->program.events[s->cursor].delta-s->remaining/s->tempo;
    /* Remaining may include a fractional tick: floor elapsed, not ceil. */
    if(s->remaining%s->tempo) --partial;
    return s->ticks+partial;
}
uint64_t aica_sequence_beats(const aica_sequence_t *s) {
    uint64_t ticks,denom,whole,part;
    if(!s || !s->program.ticks_per_quarter) return 0;
    ticks=aica_sequence_ticks(s);denom=(uint64_t)s->program.ticks_per_quarter*4;
    if(s->beat_multiply) return ticks/(denom<<s->beat_shift);
    whole=ticks/denom;part=((ticks%denom)<<s->beat_shift)/denom;
    if(whole>(UINT64_MAX>>s->beat_shift)) return UINT64_MAX;
    whole<<=s->beat_shift;return part>UINT64_MAX-whole?UINT64_MAX:whole+part;
}
/* Integral of the positive linear rate from0 to x. Split the quotient before
   multiplication so the largest32767ms fade cannot overflow64 bits. */
static uint64_t area(const aica_sequence_t *s,uint32_t x) {
    int64_t delta=(int64_t)s->speed_target-s->speed_from;
    uint64_t square=(uint64_t)x*x,denom=(uint64_t)s->speed_duration*2;
    uint64_t magnitude=(uint64_t)(delta<0?-delta:delta),change;
    change=(square/denom)*magnitude+((square%denom)*magnitude)/denom;
    return delta<0?(uint64_t)s->speed_from*x-change:(uint64_t)s->speed_from*x+change;
}
static uint64_t advance_speed(aica_sequence_t *s,uint32_t us) {
    uint64_t result=0;
    if(s->speed_elapsed<s->speed_duration) {
        uint32_t left=s->speed_duration-s->speed_elapsed,step=us<left?us:left;
        result=area(s,s->speed_elapsed+step)-area(s,s->speed_elapsed);
        s->speed_elapsed+=step;us-=step;
    }
    return result+(uint64_t)us*s->speed_target;
}
static void fail(aica_sequence_t *s) { aica_sequence_stop(s);s->state=AICA_SEQ_FAULT; }
static void switch_chain(aica_sequence_t *s) {
    aica_seq_program_t next=s->chained;
    uint64_t whole=s->pending/s->program.ticks_per_quarter;
    uint64_t fraction=((s->pending%s->program.ticks_per_quarter)*4096+
        s->speed_remainder)*next.ticks_per_quarter/s->program.ticks_per_quarter;
    if(whole>UINT64_MAX/next.ticks_per_quarter ||
       fraction/4096>UINT64_MAX-whole*next.ticks_per_quarter) { fail(s);return; }
    s->pending=whole*next.ticks_per_quarter+fraction/4096;
    s->speed_remainder=(uint32_t)(fraction%4096);
    s->ops.release(s->ops.context,&s->program);s->chain=0;
    begin(s,&next,s->priority); /* The chained program's pin is transferred. */
}
unsigned aica_sequence_service(aica_sequence_t *s,uint32_t us) {
    uint64_t add;unsigned done=0;
    if(!s) return 0;
    add=advance_speed(s,us);
    if(s->state!=AICA_SEQ_RUNNING) return 0;
    add=add*s->program.ticks_per_quarter+s->speed_remainder;
    s->speed_remainder=(uint32_t)(add%4096);add/=4096;
    if(add>UINT64_MAX-s->pending) { fail(s);return 0; }
    s->pending+=add;
    while(done<AICA_SEQ_BUDGET && s->state==AICA_SEQ_RUNNING) {
        const aica_seq_event_t *e=&s->program.events[s->cursor];
        if(!s->loaded) { s->remaining=(uint64_t)e->delta*s->tempo;s->loaded=1;s->counted=0; }
        if(s->remaining>s->pending) { s->remaining-=s->pending;s->pending=0;break; }
        s->pending-=s->remaining;s->remaining=0;
        if(!s->counted) {
            if(e->delta>UINT64_MAX-s->ticks) { fail(s);break; }
            s->ticks+=e->delta;s->counted=1;
        }
        if(e->kind==AICA_SEQ_MIDI) {
            int r=s->ops.emit(s->ops.context,e->status,e->a,e->b,s->priority);
            if(!r) break;
            if(r<0) { fail(s);break; }
        }
        else if(e->kind==AICA_SEQ_TEMPO) s->tempo=e->value;
        else if(e->kind==AICA_SEQ_LOOP) {
            s->loop_cursor[s->depth]=s->cursor+1;s->loop_remaining[s->depth]=e->value;++s->depth;
        }
        else if(e->kind==AICA_SEQ_LOOP_END) {
            unsigned depth=(unsigned)s->depth-1;
            if(s->exit_loop && s->chain) { switch_chain(s);++done;continue; }
            if(!s->exit_loop && s->loop_remaining[depth]!=1) {
                if(s->loop_remaining[depth]) --s->loop_remaining[depth];
                s->cursor=s->loop_cursor[depth];s->loaded=0;++done;continue;
            }
            --s->depth;if(!s->depth) s->exit_loop=0;
        }
        else if(e->kind==AICA_SEQ_END) {
            if(s->chain) switch_chain(s);else aica_sequence_stop(s);
            ++done;continue;
        }
        ++s->cursor;s->loaded=0;++done;
    }
    if(done>UINT32_MAX-s->processed) s->processed=UINT32_MAX;else s->processed+=done;
    return done;
}
