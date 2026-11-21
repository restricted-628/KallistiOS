#include <kosdec/adx_sfa.h>
#include <kosdec/adx_joint_reader.h>
#include <kosdec/adx_stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"sfa:%u %s\n",(unsigned)__LINE__,#x); abort(); } } while(0)
static void run(unsigned channels,unsigned capacity,unsigned extra,uint32_t frames,
                unsigned length_mode, unsigned maximum) {
    uint8_t body[108],reference[414],ring[260];
    unsigned groups=(frames+31)/32,width=18*channels;
    memset(body,0,sizeof(body));
    /* Two-group jobs carry float history across a block boundary. Keep the
       preceding groups silent when comparing a two-group job with strict PCM. */
    for(unsigned g=0;g<groups;++g) if(g!=1 && (maximum<64 || g+1==groups))
      for(unsigned ch=0;ch<channels;++ch) {
        body[g*width+18*ch+1]=55;
        memset(body+g*width+18*ch+2,0x12+ch,16);
    }
    CHECK(kosdec_adx_make_sfa_header(reference,288,channels,32000,frames)==KOSDEC_ADX_DONE);
    memcpy(reference+288,body,groups*width);
    memset(reference+288+groups*width,0,18);
    reference[288+groups*width]=0x80;reference[289+groups*width]=1;
    reference[291+groups*width]=14;
    size_t bytes=306+groups*width,used=0,expected_frames=0;
    int16_t expected[192],actual[192];
    struct kosdec_adx_decoder decoder;kosdec_adx_init(&decoder);
    for(unsigned n=0;n<100;++n) {
        struct kosdec_adx_result r=kosdec_adx_decode(&decoder,reference+used,bytes-used,
            expected+channels*expected_frames,32,true);
        used+=r.consumed;expected_frames+=r.frames;
        if(r.status>=KOSDEC_ADX_DONE) { CHECK(r.status==KOSDEC_ADX_DONE);break; }
    }
    CHECK(expected_frames==frames);
    struct kosdec_adx_joint joint;
    CHECK(kosdec_adx_joint_ring_init(&joint,ring,capacity,extra)==KOSDEC_ADX_JOINT_OK);
    struct kosdec_adx_sfa writer;
    struct kosdec_adx_joint_reader reader;
    int16_t job_pcm[128];
    CHECK(kosdec_adx_joint_reader_init(&reader,&joint)==KOSDEC_ADX_JOINT_OK);
    if(maximum && length_mode==1)
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,job_pcm,128,maximum,31,false)==KOSDEC_ADX_DONE);
    if(length_mode) {
        CHECK(kosdec_adx_sfa_init_live(&writer,&reader,channels,32000)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_sfa_set_length(&writer,UINT32_MAX)==KOSDEC_ADX_BAD_ARGUMENT);
        if(length_mode==1)
            CHECK(kosdec_adx_sfa_set_length(&writer,frames)==KOSDEC_ADX_DONE);
    }
    else CHECK(kosdec_adx_sfa_init(&writer,&joint,channels,32000,frames)==KOSDEC_ADX_DONE);
    if(maximum && length_mode==2)
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,job_pcm,128,maximum,31,false)==KOSDEC_ADX_DONE);
    uint32_t accepted=99;
    if(groups) CHECK(kosdec_adx_sfa_write(&writer,body,1,&accepted)==KOSDEC_ADX_NEED_OUTPUT && !accepted);
    size_t got=0;unsigned submitted=0;bool done=false;
    for(unsigned n=0;n<1000;++n) {
        enum kosdec_adx_status status;
        if(!writer.length_known && reader.decoder.header_ready && got==frames/32u*32u) {
            CHECK(reader.decoder.total_frames==KOSDEC_ADX_MAX_FRAMES);
            if(got) CHECK(kosdec_adx_sfa_set_length(&writer,(uint32_t)got-1)==KOSDEC_ADX_BAD_ARGUMENT);
            CHECK(kosdec_adx_sfa_set_length(&writer,frames)==KOSDEC_ADX_DONE);
            CHECK(kosdec_adx_sfa_set_length(&writer,frames)==KOSDEC_ADX_BAD_ARGUMENT);
        }
        unsigned publish_limit=writer.length_known?groups:frames/32u;
        if(writer.header_used==288 && submitted<publish_limit) {
            accepted=99;
            status=submitted==1?kosdec_adx_sfa_insert(&writer,1,&accepted):
                kosdec_adx_sfa_write(&writer,body+submitted*width,1,&accepted);
            CHECK(status==KOSDEC_ADX_MORE || status==KOSDEC_ADX_NEED_OUTPUT);
            CHECK(accepted<=1);submitted+=accepted;
        }
        else {
            status=kosdec_adx_sfa_pump(&writer);
            CHECK(status==KOSDEC_ADX_MORE || status==KOSDEC_ADX_NEED_OUTPUT || status==KOSDEC_ADX_DONE);
        }
        if(n%3==0) continue; /* Deliberate consumer backpressure. */
        int16_t chunk[128];
        unsigned room = maximum ? maximum : 32;
        struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,chunk,room);
        CHECK(r.frames<=room && r.frames<=frames-got);
        memcpy(actual+channels*got,chunk,r.frames*channels*sizeof(*chunk));
        got+=r.frames;
        if(r.status>=KOSDEC_ADX_DONE) { CHECK(r.status==KOSDEC_ADX_DONE);done=true;break; }
    }
    CHECK(done && writer.done && submitted==groups && got==frames);
    CHECK(reader.decoder.total_frames==frames);
    CHECK(reader.consumed_bytes==bytes);
    CHECK(!memcmp(actual,expected,got*channels*sizeof(int16_t)));
    CHECK(kosdec_adx_sfa_pump(&writer)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
}

static void job_endings(unsigned fault) {
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_reader reader;
    struct kosdec_adx_sfa writer;
    struct kosdec_adx_chunk chunk;
    uint8_t ring[512 + 35];
    int16_t scratch[128], pcm[128];
    CHECK(kosdec_adx_joint_ring_init(&joint,ring,512,35)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_init(&reader,&joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,scratch,128,64,31,false)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_sfa_init_live(&writer,&reader,2,32000)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_sfa_pump(&writer)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_joint_reader_read(&reader,pcm,64).status==KOSDEC_ADX_MORE);
    uint32_t accepted;
    if(fault!=0) {
        CHECK(kosdec_adx_sfa_set_length(&writer,1)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_sfa_insert(&writer,1,&accepted)==KOSDEC_ADX_MORE && accepted==1);
        struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,pcm,64);
        CHECK(r.status==KOSDEC_ADX_MORE && r.frames==1);
        r=kosdec_adx_joint_reader_read(&reader,pcm,64);
        CHECK(r.status==KOSDEC_ADX_NEED_INPUT && !r.frames);
    }
    /* Inject transport damage below the writer to exercise reader checks. */
    unsigned bytes = fault==1 ? 17 : fault==2 ? 19 : 18;
    CHECK(kosdec_adx_joint_get(&joint,KOSDEC_ADX_JOINT_FREE,bytes,&chunk)==KOSDEC_ADX_JOINT_OK);
    CHECK(chunk.bytes==bytes);
    memset(chunk.data,0,bytes);
    chunk.data[0]=0x80;
    chunk.data[1]=1;
    chunk.data[3]=14;
    if(fault==3) chunk.data[17]=1;
    CHECK(kosdec_adx_joint_put(&joint,&chunk)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_finish(&joint)==KOSDEC_ADX_JOINT_OK);
    enum kosdec_adx_status expected=fault==1?KOSDEC_ADX_TRUNCATED:KOSDEC_ADX_INVALID;
    for(unsigned i=0;i<2;++i) {
        struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,pcm,64);
        CHECK(r.status==expected && !r.frames);
    }
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
}

struct mux_input {
    const uint8_t *bytes;
    unsigned size, fragment, calls;
};

static struct kosdec_adx_input_result mux_read(void *ctx, uint32_t offset,
                                              uint8_t *out, size_t size) {
    struct mux_input *in = ctx;
    CHECK(offset < in->size && size <= in->size - offset && size <= 512);
    if(++in->calls % 4 == 0)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(size > in->fragment) size = in->fragment;
    memcpy(out,in->bytes + offset,size);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,size};
}

static void mux_record(unsigned channels, unsigned fragment, bool stop_at_endcode) {
    enum { FRAMES = 33, HEADER = KOSDEC_ADX_SFA_HEADER_BYTES };
    unsigned body_end = HEADER + 2 * 18 * channels;
    unsigned record_end = body_end + 18;
    uint8_t payload[HEADER + 72 + 18 + 23] = {0};
    CHECK(kosdec_adx_make_sfa_header(payload,HEADER,channels,44100,FRAMES)==KOSDEC_ADX_DONE);
    payload[body_end] = 0x80;
    payload[body_end + 1] = 1;
    payload[body_end + 3] = 14;
    /* Original mixed test padding. The demux owns bytes after the endcode. */
    memset(payload + record_end,0xff,sizeof(payload) - record_end);
    payload[record_end + 2] = 0x31;
    struct mux_input input = {payload,sizeof(payload),fragment,0};
    struct kosdec_adx_stream stream;
    kosdec_adx_stream_init(&stream,mux_read,&input,input.size,false);
    if(stop_at_endcode) {
        CHECK(kosdec_adx_stream_set_linked(&stream,true)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_stream_release_link(&stream)==KOSDEC_ADX_MORE);
    }
    unsigned frames = 0;
    enum kosdec_adx_status status = KOSDEC_ADX_MORE;
    for(unsigned n = 0; n < 2000 && status < KOSDEC_ADX_DONE; ++n) {
        int16_t pcm[14];
        unsigned calls = input.calls;
        struct kosdec_adx_stream_result part = kosdec_adx_stream_read(&stream,pcm,7);
        CHECK(input.calls - calls <= 1 && part.frames <= 7);
        for(size_t i = 0; i < part.frames * channels; ++i) CHECK(pcm[i]==0);
        frames += (unsigned)part.frames;
        status = part.status;
    }
    CHECK(frames==FRAMES);
    CHECK(status==(stop_at_endcode?KOSDEC_ADX_DONE:KOSDEC_ADX_INVALID));
    if(stop_at_endcode) CHECK(stream.byte_position==record_end && stream.linked_entries==1);
}

int main(void) {
    for(unsigned fault=0;fault<4;++fault) job_endings(fault);
    for(unsigned channels = 1; channels <= 2; ++channels)
        for(unsigned fragment = 1; fragment <= 512; fragment *= 8)
            for(unsigned mode = 0; mode <= 1; ++mode)
                mux_record(channels,fragment,mode != 0);
    for(unsigned channels=1;channels<=2;++channels)
        for(unsigned capacity=36;capacity<=130;++capacity)
            for(unsigned extra=0;extra<=1;++extra)
                for(unsigned f=0;f<4;++f)
                    for(unsigned mode=0;mode<3;++mode)
                        run(channels,capacity,extra?capacity:0,f==0?0:f==1?1:f==2?32:65,mode,0);
    for(unsigned channels=1;channels<=2;++channels)
        for(unsigned capacity=36;capacity<=130;++capacity)
            for(unsigned frames=0;frames<=96;frames+=13)
                for(unsigned mode=1;mode<=2;++mode)
                    for(unsigned maximum=63;maximum<=64;++maximum)
                        run(channels,capacity,35,frames,mode,maximum);
    uint8_t ring[600],body[18]={0x80};struct kosdec_adx_joint joint;struct kosdec_adx_sfa writer;
    CHECK(kosdec_adx_joint_ring_init(&joint,ring,600,0)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_sfa_init(&writer,&joint,1,32000,32)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_sfa_pump(&writer)==KOSDEC_ADX_MORE);
    uint32_t accepted=99;
    CHECK(kosdec_adx_sfa_write(&writer,body,1,&accepted)==KOSDEC_ADX_INVALID && accepted==99);
    CHECK(kosdec_adx_sfa_insert(&writer,2,&accepted)==KOSDEC_ADX_BAD_ARGUMENT && accepted==99);
    CHECK(kosdec_adx_sfa_write(&writer,NULL,1,&accepted)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_sfa_insert(&writer,1,&accepted)==KOSDEC_ADX_MORE && accepted==1);
    CHECK(kosdec_adx_sfa_pump(&writer)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
    printf("ADX_SFA checks=%u failures=0\n",checks);return 0;
}
