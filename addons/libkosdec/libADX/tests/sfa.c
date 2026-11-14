#include <kosdec/adx_sfa.h>
#include <kosdec/adx_joint_reader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"sfa:%u %s\n",(unsigned)__LINE__,#x); abort(); } } while(0)
static void run(unsigned channels,unsigned capacity,unsigned extra,uint32_t frames) {
    uint8_t body[108],reference[414],ring[260];
    unsigned groups=(frames+31)/32,width=18*channels;
    memset(body,0,sizeof(body));
    for(unsigned g=0;g<groups;++g) if(g!=1) for(unsigned ch=0;ch<channels;++ch) {
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
    CHECK(kosdec_adx_sfa_init(&writer,&joint,channels,32000,frames)==KOSDEC_ADX_DONE);
    uint32_t accepted=99;
    if(groups) CHECK(kosdec_adx_sfa_write(&writer,body,1,&accepted)==KOSDEC_ADX_NEED_OUTPUT && !accepted);
    struct kosdec_adx_joint_reader reader;
    CHECK(kosdec_adx_joint_reader_init(&reader,&joint)==KOSDEC_ADX_JOINT_OK);
    size_t got=0;unsigned submitted=0;bool done=false;
    for(unsigned n=0;n<1000;++n) {
        enum kosdec_adx_status status;
        if(writer.header_used==288 && submitted<groups) {
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
        struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,actual+channels*got,32);
        got+=r.frames;
        if(r.status>=KOSDEC_ADX_DONE) { CHECK(r.status==KOSDEC_ADX_DONE);done=true;break; }
    }
    CHECK(done && writer.done && submitted==groups && got==frames);
    CHECK(!memcmp(actual,expected,got*channels*sizeof(int16_t)));
    CHECK(kosdec_adx_sfa_pump(&writer)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
}
int main(void) {
    for(unsigned channels=1;channels<=2;++channels)
        for(unsigned capacity=36;capacity<=130;++capacity)
            for(unsigned extra=0;extra<=1;++extra)
                for(unsigned f=0;f<3;++f) run(channels,capacity,extra?capacity:0,f==0?0:f==1?32:65);
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
