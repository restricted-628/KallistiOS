/* Original native music policy; intentionally not a vendor-driver translation. */
#include "aica_music.h"
#include <stddef.h>
#include <limits.h>
static void clear(void *p,unsigned n) { uint8_t *b=p;while(n--) *b++=0; }
static int clamp(int v,int lo,int hi) { return v<lo?lo:v>hi?hi:v; }
static void increment(uint32_t *p) { if(*p!=UINT32_MAX) ++*p; }
static aica_music_port_t *port(aica_music_t *m,unsigned p,uint32_t g) {
    if(!m || p>=AICA_MUSIC_PORTS || !g || !m->ports[p].open || m->ports[p].generation!=g)
        return NULL;
    return &m->ports[p];
}
static void channel_defaults(aica_music_channel_t *c) {
    clear(c,sizeof(*c));c->volume=100;c->expression=127;c->pan=64;
    c->bend_semitones=2;c->rpn_msb=c->rpn_lsb=127;
}
uint32_t aica_music_rate(uint32_t rate,int32_t pitch) {
    static const uint32_t ratios[13]={
#include "aica_music_pitch.inc"
    };
    int32_t octave,remainder,note,fraction;
    uint32_t ratio;uint64_t scaled;
    if(!rate) return 1;
    if(rate>192000) rate=192000;
    /* Domain bounds avoid both signed division overflow and oversized shifts. */
    if(pitch<=-16*12*256) return 1;
    if(pitch>=16*12*256) return 192000;
    octave=pitch/(12*256);remainder=pitch%(12*256);
    if(remainder<0) { remainder+=12*256;--octave; }
    note=remainder/256;fraction=remainder%256;
    ratio=ratios[note]+(uint32_t)(((uint64_t)(ratios[note+1]-ratios[note])*(uint32_t)fraction+128)/256);
    scaled=(uint64_t)rate*ratio;
    if(octave>=0) scaled<<=(unsigned)octave;
    else scaled>>=(unsigned)-octave;
    scaled=(scaled+(1u<<19))>>20;
    return scaled<1?1:scaled>192000?192000:(uint32_t)scaled;
}
static void dirty(aica_music_t *m,unsigned p,int ch) {
    unsigned i;
    for(i=0;i<m->voice_count;i++)
        if(m->voices[i].state && m->voices[i].port==p &&
           (ch<0 || m->voices[i].channel==(unsigned)ch)) m->voices[i].dirty=1;
}
static void stop_voice(aica_music_t *m,unsigned i,int release) {
    aica_music_voice_t *v=&m->voices[i];
    if(!v->state) return;
    m->ops.stop(m->ops.context,i,release);
    v->state=release?AICA_MUSIC_RELEASE:AICA_MUSIC_IDLE;v->dirty=0;
}
static void stop_channel(aica_music_t *m,unsigned p,int ch,int release,int respect_sustain) {
    unsigned i;
    for(i=0;i<m->voice_count;i++) {
        aica_music_voice_t *v=&m->voices[i];
        if(!v->state || v->port!=p || (ch>=0 && v->channel!=(unsigned)ch)) continue;
        if(respect_sustain && m->ports[p].channels[v->channel].sustain &&
           v->state==AICA_MUSIC_HELD) v->state=AICA_MUSIC_SUSTAINED;
        else stop_voice(m,i,release);
    }
}
int aica_music_init(aica_music_t *m,unsigned voices,const aica_music_ops_t *ops) {
    if(!m || !ops || !voices || voices>64 || !ops->resolve || !ops->retain ||
       !ops->start || !ops->update || !ops->stop) return AICA_MUSIC_INVALID;
    clear(m,sizeof(*m));m->voice_count=(uint8_t)voices;m->ops=*ops;return 0;
}
int aica_music_open(aica_music_t *m,unsigned p,uint32_t bank,uint32_t generation,uint32_t *out) {
    unsigned i;aica_music_port_t *v;uint32_t g;
    if(!m || p>=8 || bank>=128 || !generation || !out) return AICA_MUSIC_INVALID;
    v=&m->ports[p];
    if(m->quiescing || v->open) return AICA_MUSIC_BUSY;
    if(v->generation==UINT32_MAX) return AICA_MUSIC_EXHAUSTED;
    g=v->generation+1;clear(v,sizeof(*v));v->generation=g;v->open=1;
    v->bank=bank;v->bank_generation=generation;v->release_mode=1;
    for(i=0;i<16;i++) channel_defaults(&v->channels[i]);
    *out=g;return 0;
}
int aica_music_close(aica_music_t *m,unsigned p,uint32_t g) {
    aica_music_port_t *v=port(m,p,g);
    if(!v) return AICA_MUSIC_INVALID;
    stop_channel(m,p,-1,0,0);v->open=0;return 0;
}
int aica_music_message(aica_music_t *m,unsigned p,uint32_t g,unsigned status,
                       unsigned a,unsigned b,unsigned priority) {
    aica_music_port_t *v=port(m,p,g);aica_music_event_t *e;
    if(!v || status<0x80 || status>=0xf0 || a>127 || b>127 || priority>31)
        return AICA_MUSIC_INVALID;
    if(m->faulted) return AICA_MUSIC_FAULT;
    if(m->quiescing || m->count==AICA_MUSIC_QUEUE) return AICA_MUSIC_BUSY;
    e=&m->queue[(m->head+m->count)%AICA_MUSIC_QUEUE];
    *e=(aica_music_event_t){.generation=g,.bank=v->bank,.bank_generation=v->bank_generation,
        .port=(uint8_t)p,.channel=(uint8_t)(status&15),.status=(uint8_t)(status&0xf0),
        .a=(uint8_t)a,.b=(uint8_t)b,.priority=(uint8_t)priority};
    ++m->count;increment(&m->admitted);return 0;
}
int aica_music_silence(aica_music_t *m,unsigned p,uint32_t g,unsigned release) {
    unsigned read,kept=0;
    if(!port(m,p,g) || release>1) return AICA_MUSIC_INVALID;
    for(read=0;read<m->count;read++) {
        aica_music_event_t *e=&m->queue[(m->head+read)%AICA_MUSIC_QUEUE];
        if(e->port==p && e->generation==g && (e->status==0x80 || e->status==0x90)) {
            if(!read && m->ops.cancel_resolve) m->ops.cancel_resolve(m->ops.context);
        }
        else m->queue[(m->head+kept++)%AICA_MUSIC_QUEUE]=*e;
    }
    m->count=(uint16_t)kept;stop_channel(m,p,-1,(int)release,0);return 0;
}
int aica_music_mute(aica_music_t *m,unsigned p,uint32_t g,unsigned mask) {
    aica_music_port_t *v=port(m,p,g);unsigned ch;
    if(!v || mask>65535) return AICA_MUSIC_INVALID;
    v->mute=(uint16_t)mask;
    for(ch=0;ch<16;ch++) if(mask&(1u<<ch)) stop_channel(m,p,(int)ch,0,0);
    return 0;
}
int aica_music_gm(aica_music_t *m,unsigned p,uint32_t g,unsigned enabled) {
    aica_music_port_t *v=port(m,p,g);
    if(!v || enabled>1) return AICA_MUSIC_INVALID;
    v->gm=(uint8_t)enabled;return 0;
}
int aica_music_release_mode(aica_music_t *m,unsigned p,uint32_t g,unsigned enabled) {
    aica_music_port_t *v=port(m,p,g);
    if(!v || enabled>1) return AICA_MUSIC_INVALID;
    v->release_mode=(uint8_t)enabled;return 0;
}
int aica_music_parameter(aica_music_t *m,unsigned p,uint32_t g,unsigned parameter,
                         int value,uint32_t duration) {
    aica_music_port_t *v=port(m,p,g);
    if(!v || parameter>4 || duration>32767 ||
       (parameter==AICA_MUSIC_PITCH ? value < -32768 || value>32767 : value < -127 || value>127) ||
       (parameter>=3 && duration)) return AICA_MUSIC_INVALID;
    if(parameter<3) {
        aica_music_fade_t *f=&v->fades[parameter];
        f->from=f->value;f->target=value;f->elapsed=0;f->duration=duration;
        if(!duration) f->value=value;
    }
    else if(parameter==3) v->direct=(int16_t)value;
    else v->effect=(int16_t)value;
    dirty(m,p,-1);return 0;
}
int aica_music_reset(aica_music_t *m,unsigned p,uint32_t g) {
    aica_music_port_t *v=port(m,p,g);unsigned i;
    if(!v) return AICA_MUSIC_INVALID;
    for(i=0;i<3;i++) clear(&v->fades[i],sizeof(v->fades[i]));
    v->direct=v->effect=0;v->mute=0;dirty(m,p,-1);return 0;
}
static void config(aica_music_t *m,const aica_music_voice_t *v,aica_channel_config_t *out) {
    const aica_music_port_t *p=&m->ports[v->port];
    const aica_music_channel_t *c=&p->channels[v->channel];
    uint32_t volume;int pitch;
    *out=v->tone.config;
    pitch=((int)v->key-(int)v->tone.root_key)*256+(int)v->tone.fine_cents*256/100+
        p->fades[AICA_MUSIC_PITCH].value+
        (int)c->bend*((int)c->bend_semitones*256+(int)c->bend_cents*256/100)/8192;
    out->freq=aica_music_rate(out->freq,pitch);
    volume=(uint32_t)clamp((int)out->vol+p->fades[AICA_MUSIC_VOLUME].value*2,0,255);
    volume=(volume*v->velocity+63)/127;
    volume=(volume*c->volume+63)/127;
    out->vol=(volume*c->expression+63)/127;
    out->pan=(uint32_t)clamp((int)out->pan+((int)c->pan-64)*2+p->fades[AICA_MUSIC_PAN].value*2,0,255);
    out->direct_level=(uint32_t)clamp((int)out->direct_level+p->direct*15/127,0,15);
    out->effect_send=(uint32_t)clamp((int)out->effect_send+p->effect*15/127,0,15);
    out->pitch_lfo_depth=(uint32_t)clamp((int)out->pitch_lfo_depth+c->modulation*7/127,0,7);
}
static int choose(aica_music_t *m,unsigned incoming) {
    unsigned i;int selected=-1;
    for(i=0;i<m->voice_count;i++) {
        aica_music_voice_t *v=&m->voices[i],*best;
        if(!v->state) return (int)i;
        if(incoming && v->priority>incoming) continue;
        if(selected<0) { selected=(int)i;continue; }
        best=&m->voices[selected];
        /* Original native policy: released first, then lower priority, then
           older age; equal ties retain the lowest pool index. */
        if((v->state==AICA_MUSIC_RELEASE)!=(best->state==AICA_MUSIC_RELEASE)) {
            if(v->state==AICA_MUSIC_RELEASE) selected=(int)i;
        }
        else if(v->priority<best->priority ||
                (v->priority==best->priority && v->age>best->age)) selected=(int)i;
    }
    return selected;
}
static void note_off(aica_music_t *m,const aica_music_event_t *e) {
    unsigned i;aica_music_port_t *p=&m->ports[e->port];
    for(i=0;i<m->voice_count;i++) {
        aica_music_voice_t *v=&m->voices[i];
        if((v->state!=AICA_MUSIC_HELD && v->state!=AICA_MUSIC_SUSTAINED) ||
           v->port!=e->port || v->channel!=e->channel || v->key!=e->a ||
           v->port_generation!=e->generation) continue;
        if(p->channels[e->channel].sustain) v->state=AICA_MUSIC_SUSTAINED;
        else stop_voice(m,i,p->release_mode);
    }
}
static void controller(aica_music_t *m,const aica_music_event_t *e) {
    aica_music_port_t *p=&m->ports[e->port];
    aica_music_channel_t *c=&p->channels[e->channel];unsigned i;
    switch(e->a) {
        case 1:c->modulation=e->b;break;
        case 7:c->volume=e->b;break;
        case 10:c->pan=e->b;break;
        case 11:c->expression=e->b;break;
        case 32:c->bank=e->b;break;
        case 48:p->mailbox=e->b;break;
        case 64:
            c->sustain=(uint8_t)(e->b>=64);
            if(!c->sustain) for(i=0;i<m->voice_count;i++) {
                aica_music_voice_t *v=&m->voices[i];
                if(v->state==AICA_MUSIC_SUSTAINED && v->port==e->port && v->channel==e->channel)
                    stop_voice(m,i,p->release_mode);
            }
            break;
        case 100:c->rpn_lsb=e->b;break;
        case 101:c->rpn_msb=e->b;break;
        case 98:case 99:c->rpn_msb=c->rpn_lsb=127;break;
        case 6:if(!c->rpn_msb && !c->rpn_lsb) c->bend_semitones=e->b;break;
        case 38:if(!c->rpn_msb && !c->rpn_lsb) c->bend_cents=(uint8_t)(e->b>99?99:e->b);break;
        case 120:stop_channel(m,e->port,e->channel,0,0);break;
        case 121: {
            uint8_t program=c->program,bank=c->bank;
            channel_defaults(c);c->program=program;c->bank=bank;
            for(i=0;i<m->voice_count;i++) if(m->voices[i].state==AICA_MUSIC_SUSTAINED &&
                m->voices[i].port==e->port && m->voices[i].channel==e->channel)
                stop_voice(m,i,p->release_mode);
            break;
        }
        case 123:stop_channel(m,e->port,e->channel,p->release_mode,1);break;
        default:return; /* Explicit baseline unsupported controller no-op. */
    }
    dirty(m,e->port,e->channel);
}
static int event(aica_music_t *m,const aica_music_event_t *e) {
    aica_music_port_t *p=port(m,e->port,e->generation);
    aica_music_channel_t *c;unsigned i;int rc,index;
    aica_music_tone_t tone;aica_music_voice_t next;aica_channel_config_t output;
    if(!p || p->bank!=e->bank || p->bank_generation!=e->bank_generation) return AICA_MUSIC_INVALID;
    c=&p->channels[e->channel];
    switch(e->status) {
        case 0x80:note_off(m,e);return 0;
        case 0x90:
            if(!e->b) { note_off(m,e);return 0; }
            if(p->mute&(1u<<e->channel)) return 0;
            rc=m->ops.resolve(m->ops.context,e,c->bank,c->program,p->gm && e->channel==9,&tone);
            if(rc) return rc;
            if(tone.bank!=e->bank || tone.generation!=e->bank_generation ||
               tone.root_key>127 || tone.fine_cents < -100 || tone.fine_cents>100 ||
               tone.reserved || !tone.config.freq || tone.config.freq>192000)
                return AICA_MUSIC_INVALID;
            index=choose(m,e->priority);
            if(index<0) { increment(&m->declined);return 0; }
            if(m->token==UINT32_MAX) return AICA_MUSIC_EXHAUSTED;
            i=(unsigned)index;
            clear(&next,sizeof(next));next.tone=tone;next.token=m->token+1;
            next.port_generation=e->generation;next.state=AICA_MUSIC_HELD;
            next.port=e->port;next.channel=e->channel;next.key=e->a;
            next.velocity=e->b;next.priority=e->priority;config(m,&next,&output);
            rc=m->ops.retain(m->ops.context,i,&tone);if(rc) return rc;
            /* A failing start still owns its physical lease. No logical success
               or release follows from an uncertain hardware callback. */
            ++m->token;rc=m->ops.start(m->ops.context,i,next.token,&output);
            if(rc) { m->faulted=1;m->fault_voice=(uint8_t)(i+1);return AICA_MUSIC_FAULT; }
            if(m->voices[i].state) increment(&m->stolen);
            m->voices[i]=next;return 0;
        case 0xb0:controller(m,e);return 0;
        case 0xc0:c->program=e->a;return 0;
        case 0xe0:c->bend=(int16_t)(((int)e->b<<7)+(int)e->a-8192);dirty(m,e->port,e->channel);return 0;
        case 0xa0:case 0xd0:return 0;
        default:return AICA_MUSIC_INVALID;
    }
}
unsigned aica_music_service(aica_music_t *m,uint32_t elapsed) {
    unsigned p,f,i,processed=0,changed=0;
    if(!m || m->faulted) return 0;
    m->tick+=elapsed; /* Informational modulo clock, never an identity. */
    for(p=0;p<8;p++) if(m->ports[p].open) for(f=0;f<3;f++) {
        aica_music_fade_t *v=&m->ports[p].fades[f];uint32_t left;
        if(v->elapsed==v->duration) continue;
        left=v->duration-v->elapsed;v->elapsed+=elapsed<left?elapsed:left;
        v->value=v->from+(int32_t)((int64_t)(v->target-v->from)*v->elapsed/v->duration);
        changed|=1u<<p;
    }
    for(i=0;i<m->voice_count;i++) if(m->voices[i].state) {
        aica_music_voice_t *v=&m->voices[i];
        if(changed&(1u<<v->port)) v->dirty=1;
        v->age=elapsed>UINT32_MAX-v->age?UINT32_MAX:v->age+elapsed;
    }
    while(processed<AICA_MUSIC_EVENTS_PER_PASS && m->count) {
        int rc=event(m,&m->queue[m->head]);
        if(rc==AICA_MUSIC_RETRY) break;
        m->last_result=rc;
        if(m->ops.cancel_resolve) m->ops.cancel_resolve(m->ops.context);
        if(rc) increment(&m->rejected);else increment(&m->applied);
        m->head=(uint16_t)((m->head+1)%AICA_MUSIC_QUEUE);--m->count;++processed;
        if(m->faulted) break;
    }
    if(m->faulted) return processed;
    for(i=0;i<AICA_MUSIC_VOICE_BUDGET && i<m->voice_count;i++) {
        unsigned slot=m->voice_cursor; aica_music_voice_t *v=&m->voices[slot];
        m->voice_cursor=(uint8_t)((slot+1)%m->voice_count);
        if(!v->state) continue;
        if(m->ops.ended && m->ops.ended(m->ops.context,slot,v->token)) {
            v->state=AICA_MUSIC_IDLE;v->dirty=0;continue;
        }
        if(v->dirty) {
            aica_channel_config_t output;config(m,v,&output);
            m->ops.update(m->ops.context,slot,&output);v->dirty=0;
        }
    }
    return processed;
}
void aica_music_quiesce(aica_music_t *m) {
    unsigned p;if(!m) return;m->quiescing=1;
    for(p=0;p<8;p++) if(m->ports[p].open) aica_music_close(m,p,m->ports[p].generation);
    /* Queued events hold no naked sample pointers; discard only after every
       port is invalidated. The bank resolver owns any in-progress producer pin. */
    m->head=m->count=0;
    /* A failed START may have touched hardware before failing, including when
       there was no previous logical voice in that slot. Keep its bank lease. */
    if(m->fault_voice) {
        m->ops.stop(m->ops.context,(unsigned)m->fault_voice-1,0);m->fault_voice=0;
    }
    if(m->ops.cancel_resolve) m->ops.cancel_resolve(m->ops.context);
}
