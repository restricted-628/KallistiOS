/* Actual firmware dispatcher/backend/observer, only MMIO faked. */
#define main observation_regression
#include "../sound-observation-test/arm.c"
#undef main
static aica_music_receipt_t music_receipt;
static aica_bank_header_t registry_header;
static aica_bank_record_t registry_records[128];
static void fresh(void) {
    reset();bind(7,1);
    banks.header=&registry_header;banks.records=registry_records;
    aica_bank_registry_init(&banks);
    aica_music_live_init(music,&banks,&music_receipt);
}
static void request(unsigned op, uint32_t cookie, uint32_t a, uint32_t b) {
    init_packet(AICA_MUSIC_WIRE_COMMAND,16,cookie);
    data[8]=7;data[9]=op;data[12]=a;data[13]=b;dispatch();
}
static aica_channel_config_t config(void) {
    aica_channel_config_t c={0};
    c.base=AICA_RAM_START;c.length=c.loopend=100;c.freq=22050;c.vol=255;c.pan=128;
    return c;
}
static void clocks(void) {
    aica_music_backend_t b={0};uint32_t tick=UINT32_MAX-1000u;
    uint64_t total=0,ms=0;uint32_t seed=913;
    CHECK(aica_music_clock_step(&b,tick)==0);
    for(unsigned i=0;i<20000;i++) {
        seed=seed*1664525u+1013904223u;
        uint32_t step=seed%5000u;tick+=step;total+=step;
        ms+=aica_music_clock_step(&b,tick);
        CHECK(ms==total*100u/441u && b.remainder==total*100u%441u);
    }
    for(unsigned rem=0;rem<441;rem++) {
        const uint32_t values[]={0,1,440,441,442,UINT32_MAX,UINT32_MAX-1};
        for(unsigned i=0;i<sizeof(values)/sizeof(*values);i++) {
            b.last_tick=17;b.clock_ready=1;b.remainder=rem;
            uint64_t n=(uint64_t)values[i]*100u+rem;
            CHECK(aica_music_clock_step(&b,17u+values[i])==n/441u);
            CHECK(b.remainder==n%441u);
        }
    }
    aica_music_clock_rebase(&b);CHECK(!b.clock_ready && !b.remainder);
    CHECK(aica_music_clock_step(&b,12345)==0 && aica_music_clock_step(&b,12345)==0);
    for(unsigned delta=0;delta<32;delta++) {
        uint32_t start=UINT32_MAX-delta;
        for(unsigned elapsed=0;elapsed<25;elapsed++)
            CHECK(aica_timer_waiting(start,start+elapsed,10)==(elapsed<=10));
    }
    fresh();music->backend.clock_ready=1;music->backend.remainder=440;
    music->backend.last_tick=UINT32_MAX-1;test_clock=UINT32_MAX;
    init_packet(AICA_CMD_SYNC_CLOCK,8,0);dispatch();
    CHECK(test_clock==0 && !music->backend.clock_ready && !music->backend.remainder);
    CHECK(aica_music_clock_step(&music->backend,0)==0);
}
static void claims(void) {
    for(unsigned n=1;n<=64;n++) {
        fresh();uint32_t lo=0,hi=0;
        /* Permutation yields fragmented masks until the complete pool. */
        for(unsigned i=0;i<n;i++) { unsigned p=(i*17u)%64; if(p<32) lo|=1u<<p;else hi|=1u<<(p-32); }
        request(AICA_MUSIC_WIRE_CLAIM,1,lo,hi);
        CHECK(music_receipt.result==0 && music->backend.voices==n && music->music.voice_count==n);
        unsigned seen=0;
        for(unsigned p=0;p<64;p++) if((p<32?lo:hi)&(1u<<(p%32))) {
            CHECK(music->backend.physical[seen++]==p);
            CHECK(aica_music_live_owned(music,p));
            CHECK(observations[p].attribution_kind==AICA_OBSERVATION_ATTR_LEGACY);
            CHECK(completions[p].state==AICA_COMPLETION_INTERFERED);
        }
        CHECK(seen==n && writes==0 && lp_reads==0);
    }
    for(unsigned which=0;which<2;which++) for(unsigned bad=0;bad<3;bad++) {
        fresh();
        uint32_t sequence=bad==0?UINT32_MAX:bad==1?UINT32_MAX-1u:3u;
        if(which) completions[63].sequence=sequence;else observations[63].sequence=sequence;
        aica_observation_channel_t saved[64];aica_completion_channel_t ends[64];
        memcpy(saved,observations,sizeof(saved));memcpy(ends,completions,sizeof(ends));
        request(AICA_MUSIC_WIRE_CLAIM,1,1,0x80000000u);
        CHECK(!music->backend.claimed && music->music.faulted && writes==0);
        CHECK(!memcmp(saved,observations,sizeof(saved)) && !memcmp(ends,completions,sizeof(ends)));
    }
}
static void backend_and_guards(void) {
    fresh();
    aica_observation_control_t *c=control(100,3);c->operation=AICA_CHANNEL_OP_START;c->config=config();
    dispatch();ack(100,3,101);CHECK(completions[3].start_token==100);
    request(AICA_MUSIC_WIRE_CLAIM,1,1u<<3,1u<<31);
    CHECK(completions[3].state==AICA_COMPLETION_INTERFERED && !aica_observer_needs_lp(&observer,3));
    aica_channel_config_t cfg=config();
    unsigned old_lp=lp_reads;
    CHECK(music->music.ops.start(music,0,1,&cfg)==0);
    CHECK(music->backend.start_token[0]==1 && statuses[3].configured);
    CHECK(observations[3].attribution_kind==AICA_OBSERVATION_ATTR_LEGACY && observations[3].attribution_token==0);
    unsigned before=writes, bad=commands_rejected;
    const uint32_t ops[]={AICA_CH_CMD_START,AICA_CH_CMD_STOP,AICA_CH_CMD_UPDATE};
    for(unsigned i=0;i<3;i++) {
        init_packet(AICA_CMD_CHAN,AICA_CMDSTR_CHANNEL_SIZE,3);
        ((aica_channel_t *)packet()->cmd_data)->cmd=ops[i]|AICA_CH_UPDATE_SET_VOL;
        dispatch();CHECK(writes==before);
    }
    init_packet(AICA_CMD_CHAN,AICA_CMDSTR_CHANNEL_SIZE,(1u<<3)|(1u<<4));
    ((aica_channel_t *)packet()->cmd_data)->cmd=AICA_CH_CMD_START|AICA_CH_START_SYNC;
    dispatch();CHECK(writes==before);
    init_packet(AICA_CMD_SYNC_CHANNELS,AICA_CMDSTR_CHANNEL_MASK_SIZE,0);
    data[8]=1u<<4;data[9]=1u<<31;dispatch();CHECK(writes==before);
    init_packet(AICA_CMD_CHANNEL_CONTROL,AICA_CMDSTR_CHANNEL_CONTROL_SIZE,3);
    ((aica_channel_control_t *)packet()->cmd_data)->operation=AICA_CHANNEL_OP_STOP;
    dispatch();CHECK(writes==before && commands_rejected==bad+6);
    c=control(102,3);dispatch();
    CHECK(writes==before && results[3].result==AICA_OBSERVATION_BAD_CONFIG && results[3].token==102);
    c=control(103,3);dispatch();CHECK(results[3].token==102 && writes==before);
    ack(102,3,104);CHECK(!results[3].token);
    CHECK(music->backend.start_token[0]==1 && lp_reads==old_lp);
    CHECK(music->music.ops.ended(music,0,1)==0);
    cfg.vol=127;music->music.ops.update(music,0,&cfg);
    CHECK(writes==before+1 && music->backend.start_token[0]==1);
    music->music.ops.stop(music,0,0);CHECK(writes==before+3 && statuses[3].config.vol==0);
    CHECK(lp_reads==old_lp);
    observations[3].sequence=UINT32_MAX-1;before=writes;
    CHECK(music->music.ops.start(music,0,2,&cfg)==AICA_MUSIC_FAULT);
    CHECK(writes==before && music->music.faulted && music->backend.start_token[0]==1);
    music->music.ops.update(music,0,&cfg);music->music.ops.stop(music,0,0);
    request(AICA_MUSIC_WIRE_QUIESCE,2,0,0);
    CHECK(writes==before && aica_music_live_owned(music,3));
    fresh();request(AICA_MUSIC_WIRE_CLAIM,1,1,0);cfg=config();
    music->wire.faulted=1;before=writes;
    CHECK(music->music.ops.start(music,0,1,&cfg)==AICA_MUSIC_FAULT);
    CHECK(music->music.faulted && writes==before);
    fresh();request(AICA_MUSIC_WIRE_CLAIM,1,1,0);cfg=config();cfg.freq=0;
    aica_observation_channel_t saved=observations[0];before=writes;
    CHECK(music->music.ops.start(music,0,1,&cfg)==AICA_MUSIC_FAULT);
    CHECK(writes==before && !memcmp(&saved,&observations[0],sizeof(saved)));
}
#include "../aica-sequence-tool/native/wire-fixture.h"
static _Alignas(32) uint8_t sequence_ram[0x200000];
static asq_receipt_t sequence_receipt;
static void *sequence_map(void *context,uint32_t address,uint32_t bytes,int write) {
    (void)context;(void)write;
    CHECK(address>=0x30000 && bytes<=0x200000-address);
    return sequence_ram+address;
}
static void sequence_publication(void) {
    fresh();asq_resource_runtime_init(sequence_resources,&sequence_receipt);
    sequence_resources->wire.map=sequence_map;
    memcpy(sequence_ram+0x30000,fixture,sizeof(fixture));
    init_packet(ASQ_WIRE_COMMAND,16,1);data[8]=7;data[9]=ASQ_WIRE_PREPARE;
    data[11]=1;data[12]=0x30000;data[13]=sizeof(fixture);data[14]=0x40000;data[15]=7;
    uint32_t rejected=commands_rejected;
    header.ready=0;dispatch();CHECK(commands_rejected==rejected+1 && !sequence_receipt.cookie);
    header.ready=1;data[8]=8;dispatch();CHECK(commands_rejected==rejected+2 && !sequence_receipt.cookie);
    data[8]=7;dispatch();CHECK(sequence_receipt.result==ASQ_WIRE_PENDING);
    unsigned passes=0;
    while(sequence_resources->wire.pending) { asq_wire_service(&sequence_resources->wire);CHECK(++passes<40); }
    CHECK(sequence_receipt.result==0 && sequence_receipt.state==ASQ_CATALOG_READY);
    init_packet(ASQ_WIRE_COMMAND,16,2);data[8]=7;data[9]=ASQ_WIRE_QUERY;data[11]=1;dispatch();
    CHECK(sequence_receipt.result==0 && sequence_receipt.programs==1 && sequence_receipt.event_count==7);
    init_packet(ASQ_WIRE_COMMAND,16,3);data[8]=7;data[9]=ASQ_WIRE_DROP;data[11]=1;dispatch();
    CHECK(sequence_receipt.result==0 && !sequence_receipt.state && !sequence_receipt.source);
    CHECK(writes==0 && lp_reads==0);
}
static void sequence_controls(void) {
    static asql_receipt_t control_receipt;
    fresh();asq_resource_runtime_init(sequence_resources,&sequence_receipt);
    asq_resource_control_init(sequence_resources,music,&banks,&control_receipt);
    registry_header.session=7;
    registry_records[0]=(aica_bank_record_t){.session=7,.generation=1,.state=AICA_BANK_ACTIVE};
    request(AICA_MUSIC_WIRE_CLAIM,1,1,0);request(AICA_MUSIC_WIRE_OPEN,2,0,1);
    CHECK(music_receipt.result==0 && music->music.ports[0].generation==1);
    init_packet(ASQL_COMMAND,16,1);data[8]=7;data[9]=ASQL_BIND;data[11]=1;
    uint32_t rejected=commands_rejected;
    header.ready=0;dispatch();CHECK(commands_rejected==rejected+1 && !control_receipt.cookie);
    header.ready=1;data[8]=8;dispatch();CHECK(commands_rejected==rejected+2 && !control_receipt.cookie);
    data[8]=7;dispatch();CHECK(control_receipt.result==ASQL_PENDING);
    asql_service(&sequence_resources->control,10);
    CHECK(control_receipt.result==0 && control_receipt.bound && music->wire.protected_ports==1);
    request(AICA_MUSIC_WIRE_MESSAGE,3,0x90,60);
    CHECK(music_receipt.result==AICA_MUSIC_BUSY && !music->music.count && !writes);
    init_packet(AICA_CMD_SYNC_CLOCK,8,0);test_clock=123;dispatch();
    CHECK(test_clock==123 && commands_rejected==rejected+3);
    init_packet(ASQL_COMMAND,16,2);data[8]=7;data[9]=ASQL_UNBIND;data[11]=1;dispatch();
    asql_service(&sequence_resources->control,11);
    CHECK(control_receipt.result==0 && !control_receipt.bound && !music->wire.protected_ports);
}
static void input_mode(uint32_t cookie, uint32_t generation, uint32_t enable) {
    init_packet(AICA_MUSIC_WIRE_COMMAND,16,cookie);
    data[8]=7;data[9]=AICA_MUSIC_WIRE_INPUT_MODE;data[11]=generation;data[12]=enable;
    dispatch();
}

static void input_words(const uint32_t *words, unsigned count) {
    CHECK(count <= sizeof(midi_words)/sizeof(*midi_words));
    memcpy(midi_words, words, count*sizeof(*words));
    midi_head=0;midi_end=count;
    aica_music_wire_service_input(&music->wire,aica_midi_input_read);
}

static void external_midi(void) {
    fresh();
    registry_header.session=7;
    registry_records[0]=(aica_bank_record_t){.session=7,.generation=1,.state=AICA_BANK_ACTIVE};
    request(AICA_MUSIC_WIRE_CLAIM,1,1,0);
    request(AICA_MUSIC_WIRE_OPEN,2,0,1);
    CHECK(music_receipt.result==0 && music->music.ports[0].generation==1);
    aica_music_live_service(music,10);
    CHECK(!midi_reads);

    input_mode(3,2,1);
    CHECK(music_receipt.result==AICA_MUSIC_INVALID && !music->wire.input_generation);
    input_mode(4,1,2);
    CHECK(music_receipt.result==AICA_MUSIC_INVALID && !music->wire.input_generation);
    input_mode(5,1,1);
    CHECK(music_receipt.result==0 && music->wire.input_generation==1);
    input_words((const uint32_t[]){0x90,60,100},3);
    CHECK(midi_reads==4 && !music->music.count);

    input_words((const uint32_t[]){0x92,61,0xf8},3);
    CHECK(!music->music.count && music->wire.input_partial);
    input_words((const uint32_t[]){100,62,101,0xc2,18,19},6);
    CHECK(music->music.count==4);
    const aica_music_event_t *e=music->music.queue;
    CHECK(e[0].port==0 && e[0].generation==1 && e[0].channel==2 &&
          e[0].status==0x90 && e[0].a==61 && e[0].b==100);
    CHECK(e[1].status==0x90 && e[1].a==62 && e[1].b==101);
    CHECK(e[2].status==0xc0 && e[2].a==18 && e[2].b==0);
    CHECK(e[3].status==0xc0 && e[3].a==19 && e[3].b==0);

    input_words((const uint32_t[]){0x490,63,100,0xc0,21},5);
    CHECK(music->music.count==4 && !music->wire.input_status);
    input_words((const uint32_t[]){72,90,0xf0,10,11,0xf7},6);
    CHECK(music->music.count==4);
    input_words((const uint32_t[]){0x80,61,0},3);
    CHECK(music->music.count==5 && e[4].status==0x80);

    input_mode(6,1,0);
    unsigned before=midi_reads;
    input_words((const uint32_t[]){0x90,64,100},3);
    CHECK(music_receipt.result==0 && midi_reads==before && !music->wire.input_generation);
    input_mode(7,1,1);
    music->wire.protected_ports=1;
    aica_music_wire_service_input(&music->wire,aica_midi_input_read);
    CHECK(midi_reads==before && !music->wire.input_generation);
}

static void coordinated_stop(void) {
    static asql_receipt_t control_receipt;
    fresh();asq_resource_runtime_init(sequence_resources,&sequence_receipt);
    asq_resource_control_init(sequence_resources,music,&banks,&control_receipt);
    sequence_resources->wire.map=sequence_map;
    memcpy(sequence_ram+0x30000,fixture,sizeof(fixture));
    init_packet(ASQ_WIRE_COMMAND,16,1);data[8]=7;data[9]=ASQ_WIRE_PREPARE;
    data[11]=1;data[12]=0x30000;data[13]=sizeof(fixture);data[14]=0x40000;data[15]=7;
    dispatch();
    unsigned passes=0;
    while(sequence_resources->wire.pending) { asq_wire_service(&sequence_resources->wire);CHECK(++passes<40); }
    CHECK(sequence_receipt.result==0);
    registry_header.session=7;
    registry_records[0]=(aica_bank_record_t){.session=7,.generation=1,.state=AICA_BANK_ACTIVE};
    request(AICA_MUSIC_WIRE_CLAIM,1,1,0);request(AICA_MUSIC_WIRE_OPEN,2,0,1);
    init_packet(ASQL_COMMAND,16,1);data[8]=7;data[9]=ASQL_BIND;data[11]=1;dispatch();
    asql_service(&sequence_resources->control,10);
    CHECK(control_receipt.result==0 && control_receipt.bound);
    init_packet(ASQL_COMMAND,16,2);data[8]=7;data[9]=ASQL_PLAY;data[11]=1;
    data[13]=1;dispatch();
    asql_service(&sequence_resources->control,11);
    CHECK(control_receipt.result==0 && control_receipt.state==AICA_SEQ_RUNNING);
    CHECK(aica_sequence_pause(&music->sequences.sequences[0])==0);

    aica_observation_control_t *c=control(100,1);c->operation=AICA_CHANNEL_OP_START;
    c->config=config();c->config.length=c->config.loopend=200;dispatch();ack(100,1,101);
    CHECK(completions[1].state==AICA_COMPLETION_ACTIVE);
    unsigned before=writes, rejected=commands_rejected;
    init_packet(AICA_CMD_STOP_ALL,8,0);
    dispatch();CHECK(commands_rejected==rejected+1 && writes==before);
    init_packet(AICA_CMD_STOP_ALL,8,7);
    packet()->misc[0]=1;
    dispatch();CHECK(commands_rejected==rejected+2 && writes==before);
    music->wire.pending=1;
    CHECK(force_stop_all()==AICA_DRIVER_ACTION_BUSY && writes==before);
    music->wire.pending=0;
    sequence_resources->control.pending=1;
    CHECK(force_stop_all()==AICA_DRIVER_ACTION_BUSY && writes==before);
    sequence_resources->control.pending=0;
    CHECK(force_stop_all()==AICA_DRIVER_ACTION_APPLIED);
    CHECK(completions[1].state==AICA_COMPLETION_STOPPED &&
          completions[1].start_token==100 && completions[1].command_token==100);
    CHECK(test_finite_stops[1].position==123 && test_finite_stops[1].start_token==100);
    CHECK(music->sequences.sequences[0].state==AICA_SEQ_IDLE &&
          aica_sequence_continue(&music->sequences.sequences[0])<0);
}

int main(void) {
    observation_regression();clocks();claims();backend_and_guards();sequence_publication();sequence_controls();
    external_midi();
    coordinated_stop();
    printf("LIVE music: %u actual-C checks passed; no hardware/retirement proof\n",checks);
    return 0;
}
