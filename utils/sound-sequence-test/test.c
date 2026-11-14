#include "aica_sequence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,refs,peak,notes,stops,pauses,resumes,refuse_pin;
static int emit_result=1,resume_result=1;
static unsigned keys[4096];
#define CHECK(x) do { ++checks;assert(x); } while(0)
static int retain(void *ctx,const aica_seq_program_t *p) {
    (void)ctx;CHECK(p->events!=NULL);if(refuse_pin) return 0;
    ++refs;if(refs>peak) peak=refs;return 1;
}
static void release(void *ctx,const aica_seq_program_t *p) {
    (void)ctx;CHECK(p->events && refs);--refs;
}
static int emit(void *ctx,unsigned status,unsigned a,unsigned b,unsigned priority) {
    (void)ctx;CHECK(refs && status>=0x80 && status<0xf0 && a<128 && b<128 && priority<32);
    if(emit_result==1) { CHECK(notes<4096);keys[notes++]=a; }return emit_result;
}
static void silence(void *ctx,int release_notes,int pause) {
    (void)ctx;CHECK(refs && (release_notes==0 || release_notes==1));
    if(pause) ++pauses;else ++stops;
}
static int resume(void *ctx) { (void)ctx;CHECK(refs);++resumes;return resume_result; }
static const aica_seq_ops_t ops={retain,release,emit,silence,resume,NULL};
static aica_seq_validation_t validate(const aica_seq_event_t *events,unsigned count,unsigned tpqn) {
    aica_seq_validation_t v;aica_seq_program_t p={events,count,tpqn,500000};
    CHECK(aica_seq_validate_begin(&v,&p)==0);
    while(!v.ready && !v.failed) { unsigned before=v.index;int r=aica_seq_validate_step(&v);
        CHECK(r>=0 && v.index-before<=32); }
    CHECK(v.ready);return v;
}
static void reset(aica_sequence_t *s) {
    CHECK(!refs);peak=notes=stops=pauses=resumes=refuse_pin=0;
    emit_result=resume_result=1;CHECK(aica_sequence_init(s,&ops)==0);
}
static const aica_seq_event_t melody[]={
    {.kind=AICA_SEQ_MIDI,.status=0x90,.a=60,.b=100},
    {.delta=480,.kind=AICA_SEQ_MIDI,.status=0x80,.a=60},
    {.kind=AICA_SEQ_TEMPO,.value=250000},
    {.delta=480,.kind=AICA_SEQ_MIDI,.status=0x90,.a=72,.b=100},
    {.kind=AICA_SEQ_END}
};
static void timing(void) {
    aica_sequence_t s;aica_seq_validation_t v=validate(melody,5,480);
    reset(&s);CHECK(aica_sequence_play(&s,&v,8)==1 && refs==1);
    CHECK(aica_sequence_service(&s,0)==1 && notes==1);
    CHECK(aica_sequence_service(&s,250000)==0 && aica_sequence_ticks(&s)==240);
    CHECK(aica_sequence_beats(&s)==0);
    CHECK(aica_sequence_service(&s,250000)==2 && notes==2 && s.tempo==250000);
    CHECK(aica_sequence_beats(&s)==1);
    CHECK(aica_sequence_pause(&s)==0 && pauses==1 && refs==1);
    CHECK(aica_sequence_service(&s,UINT32_MAX)==0 && aica_sequence_ticks(&s)==480);
    resume_result=0;CHECK(aica_sequence_continue(&s)==0 && s.state==AICA_SEQ_PAUSED);
    resume_result=1;CHECK(aica_sequence_continue(&s)==1);
    CHECK(aica_sequence_service(&s,250000)==2 && notes==3 && !refs && stops==1);
    CHECK(aica_sequence_beats(&s)==2 && s.state==AICA_SEQ_IDLE);
    CHECK(aica_sequence_beat(&s,0,4)==0 && aica_sequence_beats(&s)==8);
    CHECK(aica_sequence_beat(&s,1,0)==0 && aica_sequence_beats(&s)==0);
    CHECK(aica_sequence_beat(&s,0,7)==-1);
    CHECK(keys[0]==60 && keys[1]==60 && keys[2]==72);
}
static void backpressure(void) {
    aica_sequence_t s;aica_seq_validation_t v=validate(melody,5,480);
    reset(&s);CHECK(aica_sequence_play(&s,&v,8)==1);emit_result=0;
    CHECK(aica_sequence_service(&s,750000)==0 && !notes && !s.cursor && s.pending);
    uint64_t debt=s.pending;
    CHECK(aica_sequence_service(&s,0)==0 && s.pending==debt && s.ticks==0);
    emit_result=1;CHECK(aica_sequence_service(&s,0)==5 && notes==3 && !refs);
    CHECK(aica_sequence_play(&s,&v,8)==1);
    CHECK(aica_sequence_play(&s,&v,7)==0 && refs==1);
    refuse_pin=1;CHECK(aica_sequence_play(&s,&v,9)==-2 && refs==1 && s.priority==8);
    refuse_pin=0;CHECK(aica_sequence_play(&s,&v,0)==1 && refs==1 && peak==2);
    CHECK(aica_sequence_play(&s,&v,1)==1 && s.priority==1);
    emit_result=-1;CHECK(aica_sequence_service(&s,0)==0 && s.state==AICA_SEQ_FAULT && !refs);
    CHECK(aica_sequence_play(&s,&v,1)==-1);
}
static void loops(void) {
    aica_seq_event_t events[]={
        {.kind=AICA_SEQ_LOOP,.value=3,.auxiliary=2},
        {.delta=480,.kind=AICA_SEQ_MIDI,.status=0x90,.a=60,.b=100},
        {.kind=AICA_SEQ_LOOP_END,.value=0},
        {.kind=AICA_SEQ_MIDI,.status=0x90,.a=61,.b=100},
        {.kind=AICA_SEQ_END}};
    aica_sequence_t s;aica_seq_validation_t v=validate(events,5,480);
    reset(&s);CHECK(aica_sequence_play(&s,&v,8)==1);
    CHECK(aica_sequence_service(&s,1500000)==9 && notes==4 && !refs);
    CHECK(s.ticks==1440 && keys[3]==61);
    events[0].value=0;v=validate(events,5,480);
    reset(&s);CHECK(aica_sequence_play(&s,&v,8)==1);
    CHECK(aica_sequence_service(&s,100000000)==32 && s.state==AICA_SEQ_RUNNING);
    CHECK(aica_sequence_chain(&s,NULL)==0);
    CHECK(aica_sequence_service(&s,0)<=4 && !refs && keys[notes-1]==61);
    /* Chain to a different TPQN while preserving elapsed time debt. */
    aica_seq_event_t second[]={
        {.delta=96,.kind=AICA_SEQ_MIDI,.status=0x90,.a=77,.b=100},
        {.kind=AICA_SEQ_END}};
    aica_seq_validation_t next=validate(second,2,96);
    reset(&s);CHECK(aica_sequence_play(&s,&v,8)==1);
    CHECK(aica_sequence_chain(&s,&next)==0 && refs==2);
    CHECK(aica_sequence_service(&s,1000000)==5 && !refs && notes==2 && keys[1]==77);
    reset(&s);CHECK(aica_sequence_play(&s,&v,8)==1);
    CHECK(aica_sequence_chain(&s,&next)==0);
    refuse_pin=1;CHECK(aica_sequence_chain(&s,&v)==-2 && refs==2);
    refuse_pin=0;CHECK(aica_sequence_chain(&s,NULL)==0 && refs==1);
    aica_sequence_stop(&s);CHECK(!refs);
}
static void malformed(void) {
    aica_seq_event_t e[65];aica_seq_validation_t v;aica_seq_program_t p={e,65,480,500000};
    memset(e,0,sizeof(e));for(unsigned i=0;i<64;i++) e[i]=(aica_seq_event_t){.kind=AICA_SEQ_MIDI,.status=0x90,.a=60,.b=100};
    e[64].kind=AICA_SEQ_END;
    CHECK(aica_seq_validate_begin(&v,&p)==0);
    CHECK(aica_seq_validate_step(&v)==0 && v.index==32);
    CHECK(aica_seq_validate_step(&v)==0 && v.index==64);
    CHECK(aica_seq_validate_step(&v)==1);
    aica_sequence_t s;reset(&s);CHECK(aica_sequence_play(&s,&v,1)==1);
    CHECK(aica_sequence_service(&s,0)==32 && notes==32);
    CHECK(aica_sequence_service(&s,0)==32 && notes==64);
    CHECK(aica_sequence_service(&s,0)==1 && !refs);
    p.count=0;CHECK(aica_seq_validate_begin(&v,&p)==-1 && !v.ready);
    p.count=3;
    e[0]=(aica_seq_event_t){.kind=AICA_SEQ_LOOP,.auxiliary=1};
    e[1]=(aica_seq_event_t){.kind=AICA_SEQ_LOOP_END};
    e[2]=(aica_seq_event_t){.kind=AICA_SEQ_END};
    CHECK(aica_seq_validate_begin(&v,&p)==0);CHECK(aica_seq_validate_step(&v)==-1);
    e[1].delta=1;CHECK(aica_seq_validate_begin(&v,&p)==0);CHECK(aica_seq_validate_step(&v)==1);
    for(unsigned i=0;i<3;i++) for(unsigned byte=0;byte<16;byte++) {
        aica_seq_event_t saved=e[i];((uint8_t *)&e[i])[byte]^=0xff;
        CHECK(aica_seq_validate_begin(&v,&p)==0);
        int r;do { r=aica_seq_validate_step(&v); } while(!r);
        CHECK(r==1 || r==-1);e[i]=saved;
    }
}
static void fades(void) {
    aica_seq_event_t e[]={ {.delta=UINT32_MAX,.kind=AICA_SEQ_END} };
    aica_seq_validation_t v=validate(e,1,9600);
    aica_sequence_t a,b;
    reset(&a);CHECK(aica_sequence_init(&b,&ops)==0);
    CHECK(aica_sequence_play(&a,&v,1)==1);CHECK(aica_sequence_play(&b,&v,1)==1);
    for(int target=-4032;target<=8191;target+=4074) {
        CHECK(aica_sequence_speed(&a,target,32767)==0);
        CHECK(aica_sequence_speed(&b,target,32767)==0);
        aica_sequence_service(&a,32767000);
        for(unsigned i=0;i<32767;i++) aica_sequence_service(&b,1000);
        CHECK(a.remaining==b.remaining && a.speed_remainder==b.speed_remainder);
    }
    CHECK(aica_sequence_speed(&a,-4033,0)==-1);
    CHECK(aica_sequence_speed(&a,8192,0)==-1);
    CHECK(aica_sequence_speed(&a,0,32768)==-1);
    aica_sequence_stop(&a);aica_sequence_stop(&b);CHECK(!refs);
}
static uint32_t random_state=0x24751;
static uint32_t random32(void) {
    random_state=random_state*1664525u+1013904223u;return random_state;
}
static void timeline(void) {
    /* Independent absolute-deadline oracle across tempo changes and ties. */
    for(unsigned trial=0;trial<128;trial++) {
        aica_seq_event_t events[65];uint64_t deadlines[64],deadline=0,elapsed=0;
        unsigned tpqn=96+random32()%9505,tempo=500000,expected=0;
        int relative=(int)(random32()%10240)-2048;
        aica_sequence_t s;
        for(unsigned i=0;i<64;i++) {
            events[i]=(aica_seq_event_t){.delta=random32()%49,.kind=AICA_SEQ_MIDI,
                .status=0x90,.a=(uint8_t)(i+32),.b=100};
            deadline+=(uint64_t)events[i].delta*tempo;deadlines[i]=deadline;
            if(i%7==6) {
                tempo=1000+random32()%999001;
                events[i].kind=AICA_SEQ_TEMPO;events[i].status=events[i].a=events[i].b=0;
                events[i].value=tempo;
            }
        }
        events[64]=(aica_seq_event_t){.kind=AICA_SEQ_END};
        aica_seq_validation_t v=validate(events,65,tpqn);
        reset(&s);CHECK(aica_sequence_speed(&s,relative,0)==0);
        CHECK(aica_sequence_play(&s,&v,1)==1);
        for(unsigned pass=0;pass<1001 && s.state==AICA_SEQ_RUNNING;pass++) {
            uint32_t step=pass==1000?UINT32_MAX:50000+random32()%100001;
            elapsed+=step;
            unsigned work=aica_sequence_service(&s,step),drains=0;
            while(work==32) { CHECK(++drains<=3);work=aica_sequence_service(&s,0); }
            uint64_t units=elapsed*(uint32_t)(4096+relative)*tpqn/4096;
            expected=0;
            for(unsigned i=0;i<64;i++) if(events[i].kind==AICA_SEQ_MIDI && deadlines[i]<=units) ++expected;
            CHECK(notes==expected);
        }
        CHECK(s.state==AICA_SEQ_IDLE && !refs);
    }
}
static void nested_and_faults(void) {
    aica_seq_event_t e[]={
        {.kind=AICA_SEQ_LOOP,.value=2,.auxiliary=5},
        {.kind=AICA_SEQ_LOOP,.value=3,.auxiliary=3},
        {.delta=1,.kind=AICA_SEQ_MIDI,.status=0x90,.a=60,.b=100},
        {.kind=AICA_SEQ_LOOP_END,.value=1},
        {.delta=1,.kind=AICA_SEQ_MIDI,.status=0x90,.a=61,.b=100},
        {.kind=AICA_SEQ_LOOP_END,.value=0}, {.kind=AICA_SEQ_END}};
    aica_seq_validation_t v=validate(e,7,480);aica_sequence_t s;
    reset(&s);CHECK(aica_sequence_play(&s,&v,1)==1);
    CHECK(aica_sequence_service(&s,500000)==20 && notes==8 && s.ticks==8 && !refs);
    CHECK(aica_sequence_play(&s,&v,1)==1);s.pending=UINT64_MAX;
    CHECK(aica_sequence_service(&s,1)==0 && s.state==AICA_SEQ_FAULT && !refs);
    aica_sequence_stop(&s);CHECK(aica_sequence_play(&s,&v,1)==1);
    CHECK(aica_sequence_pause(&s)==0);resume_result=-1;
    CHECK(aica_sequence_continue(&s)==-1 && s.state==AICA_SEQ_FAULT && !refs);
}
int main(void) {
    timing();backpressure();loops();malformed();fades();timeline();nested_and_faults();
    printf("resident sequence: %u checks PASS; engine bytes=%zu\n",checks,sizeof(aica_sequence_t));return 0;
}
