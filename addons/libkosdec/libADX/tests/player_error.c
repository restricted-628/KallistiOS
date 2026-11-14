#include <kosdec/adx_player.h>
#include "player_mock.h"
#include <errno.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"player_error:%u: %s (errno=%d)\n",(unsigned)__LINE__,#x,errno); abort(); } } while(0)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_stream source;
    uint8_t *data;
    size_t bytes;
    bool input_wait, input_error;
    alignas(32) int16_t pcm[1024], scratch[257];
    alignas(32) uint8_t stack[16384];
};
struct observer {
    pthread_t owner;
    bool dispatching;
    unsigned calls;
    struct kosdec_adx_player_error last;
};
static struct kosdec_adx_input_result input(void *context,uint32_t off,uint8_t *dst,size_t cap) {
    struct fixture *f=context;
    if(f->input_error) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(f->input_wait) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>17) cap=17;
    memcpy(dst,f->data+off,cap);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static void observed(void *context,const struct kosdec_adx_player_error *error) {
    struct observer *o=context;
    CHECK(o->dispatching && pthread_equal(o->owner,pthread_self()));
    CHECK(error->code!=0);
    o->last=*error;
    ++o->calls;
    errno=ERANGE;
}
static void init_timeout(struct fixture *f,unsigned channels,uint32_t timeout) {
    memset(f,0,sizeof(*f));
    mock_reset();
    f->data=stream(50,18,channels,32,65,&f->bytes);
    memset(f->data+20,0,24);
    kosdec_adx_stream_init(&f->source,input,f,(uint32_t)f->bytes,false);
    struct kosdec_adx_player_config c={.pcm=f->pcm,.scratch=f->scratch,
        .pcm_samples=1024,.capacity=512,.scratch_samples=257,.stack=f->stack,
        .stack_bytes=sizeof(f->stack),.sound_bytes=512,
        .transfer_timeout_ms=1000,.prefill_timeout_ms=timeout};
    CHECK(kosdec_adx_player_init(&f->player,&f->source,NULL,&c)==0);
}
static void init(struct fixture *f,unsigned channels) { init_timeout(f,channels,1000); }
static struct kosdec_adx_player_error diagnostic(struct fixture *f) {
    struct kosdec_adx_player_error e;
    CHECK(kosdec_adx_player_get_error(&f->player,&e)==0);
    return e;
}
static int dispatch(struct fixture *f,struct observer *o) {
    o->dispatching=true;
    errno=EDOM;
    int r=kosdec_adx_player_dispatch_error(&f->player);
    CHECK(errno==EDOM);
    o->dispatching=false;
    return r;
}
static void ready(struct fixture *f) {
    CHECK(kosdec_adx_player_set_wait_start(&f->player,true)==0);
    CHECK(kosdec_adx_player_start(&f->player)==0);
    for(unsigned i=0;i<3000;++i) {
        bool yes=false;
        CHECK(kosdec_adx_player_poll(&f->player)==0);
        CHECK(kosdec_adx_player_is_ready_start(&f->player,&yes)==0);
        if(yes) return;
        mock_pause();
    }
    CHECK(false);
}
static void playing(struct fixture *f) {
    ready(f);
    CHECK(kosdec_adx_player_set_wait_start(&f->player,false)==0);
    CHECK(kosdec_adx_player_poll(&f->player)==0);
    CHECK(mock_starts==1);
}
static void finish(struct fixture *f) {
    mock_failure=MOCK_OK;
    mock_async_error=0;
    mock_status_failure=false;
    CHECK(kosdec_adx_player_stop(&f->player,1000)==0);
    free(f->data);
}
static void invalid(void) {
    struct fixture f;
    init(&f,1);
    struct kosdec_adx_player_error e={.code=123};
    CHECK(kosdec_adx_player_get_error(NULL,&e)<0 && errno==EINVAL && e.code==123);
    CHECK(kosdec_adx_player_get_error(&f.player,NULL)<0 && errno==EINVAL);
    CHECK(kosdec_adx_player_clear_error(NULL)<0 && errno==EINVAL);
    CHECK(kosdec_adx_player_dispatch_error(NULL)<0 && errno==EINVAL);
    CHECK(kosdec_adx_player_set_error_callback(NULL,NULL,NULL)<0 && errno==EINVAL);
    CHECK(kosdec_adx_player_set_rate(&f.player,1)<0 && errno==EINVAL);
    CHECK(kosdec_adx_player_poll(&f.player)<0 && errno==EINVAL);
    CHECK(!diagnostic(&f).code);
    finish(&f);
}
static void injection(enum mock_failure failure,unsigned channels) {
    struct fixture f;
    struct observer o={.owner=pthread_self()};
    init(&f,channels);
    CHECK(kosdec_adx_player_set_error_callback(&f.player,observed,&o)==0);
    if(failure<=MOCK_EXEC_START) {
        mock_failure=failure;
        CHECK(kosdec_adx_player_start(&f.player)<0);
    }
    else if(failure==MOCK_ALLOC || failure==MOCK_SOUND_START || failure==MOCK_CALLBACK) {
        ready(&f);
        CHECK(kosdec_adx_player_set_wait_start(&f.player,false)==0);
        mock_failure=failure;
        CHECK(kosdec_adx_player_poll(&f.player)<0);
    }
    else {
        playing(&f);
        mock_failure=failure;
        if(failure==MOCK_UPDATE) CHECK(kosdec_adx_player_set_rate(&f.player,32000)<0);
        else if(failure==MOCK_STOP || failure==MOCK_JOIN) CHECK(kosdec_adx_player_stop(&f.player,1000)<0);
        else CHECK(kosdec_adx_player_poll(&f.player)<0);
    }
    struct kosdec_adx_player_error e=diagnostic(&f);
    CHECK(e.code==(failure==MOCK_CALLBACK?EOVERFLOW:EIO));
    CHECK(o.calls==0); /* No notification from the producer or sound call. */
    CHECK(dispatch(&f,&o)==1 && o.calls==1);
    CHECK(o.last.code==e.code && o.last.decoder==e.decoder && o.last.state==e.state);
    CHECK(dispatch(&f,&o)==0 && o.calls==1);
    CHECK(kosdec_adx_player_set_error_callback(&f.player,observed,&o)==0);
    CHECK(dispatch(&f,&o)==0); /* Replacement must not rearm. */
    struct kosdec_adx_player_info before,after;
    CHECK(kosdec_adx_player_get_info(&f.player,&before)==0);
    CHECK(kosdec_adx_player_clear_error(&f.player)==0);
    CHECK(!diagnostic(&f).code);
    CHECK(kosdec_adx_player_get_info(&f.player,&after)==0);
    CHECK(before.first_error==after.first_error && before.stop_error==after.stop_error && before.state==after.state);
    CHECK(dispatch(&f,&o)==0);
    if(before.state==KOSDEC_ADX_PLAYER_FAILED) {
        CHECK(kosdec_adx_player_poll(&f.player)<0 && errno==before.first_error);
        CHECK(!diagnostic(&f).code); /* Reporting a latched state is no new fault. */
    }
    finish(&f);
    CHECK(kosdec_adx_player_get_info(&f.player,&after)==0 && after.first_error==before.first_error);
}
static void pending_and_cleanup(void) {
    struct fixture f;
    struct observer first={.owner=pthread_self()},second={.owner=pthread_self()};
    init(&f,2);
    playing(&f);
    mock_async_error=EPIPE;
    mock_failure=MOCK_STOP;
    CHECK(kosdec_adx_player_stop(&f.player,1000)<0);
    CHECK(diagnostic(&f).code==EPIPE); /* Observation precedes cleanup EIO. */
    CHECK(dispatch(&f,&first)==0); /* No callback; record stays pending. */
    CHECK(kosdec_adx_player_set_error_callback(&f.player,observed,&first)==0);
    CHECK(kosdec_adx_player_set_error_callback(&f.player,NULL,NULL)==0);
    CHECK(dispatch(&f,&first)==0);
    CHECK(kosdec_adx_player_set_error_callback(&f.player,observed,&second)==0);
    CHECK(dispatch(&f,&second)==1 && second.calls==1 && first.calls==0);
    CHECK(kosdec_adx_player_clear_error(&f.player)==0);
    CHECK(kosdec_adx_player_stop(&f.player,1000)<0);
    CHECK(diagnostic(&f).code==EIO); /* New retry failure after clear. */
    CHECK(kosdec_adx_player_clear_error(&f.player)==0);
    CHECK(dispatch(&f,&second)==0); /* Clear drops undelivered notification. */
    finish(&f);
    struct kosdec_adx_player_info i;
    CHECK(kosdec_adx_player_get_info(&f.player,&i)==0);
    CHECK(i.first_error==EPIPE && !i.stop_error && i.state==KOSDEC_ADX_PLAYER_STOPPED);
    CHECK(!diagnostic(&f).code && dispatch(&f,&second)==0);
}
static void source_fault(unsigned kind) {
    struct fixture f;
    struct observer o={.owner=pthread_self()};
    init_timeout(&f,1,kind==2?1:1000);
    if(kind==0) f.data[0]=0;
    else if(kind==1) f.input_error=true;
    else f.input_wait=true;
    CHECK(kosdec_adx_player_set_error_callback(&f.player,observed,&o)==0);
    CHECK(kosdec_adx_player_start(&f.player)==0);
    bool failed=false;
    for(unsigned n=0;n<3000;++n) {
        if(kosdec_adx_player_poll(&f.player)<0) { failed=true; break; }
        mock_pause();
    }
    CHECK(failed && o.calls==0);
    struct kosdec_adx_player_error e=diagnostic(&f);
    CHECK(e.code==(kind==2?ETIMEDOUT:EIO));
    CHECK(kind==2?e.decoder==KOSDEC_ADX_MORE:e.decoder>=KOSDEC_ADX_INVALID);
    CHECK(dispatch(&f,&o)==1);
    finish(&f);
}
static void source_join(bool cancel) {
    struct fixture f;
    init(&f,1);
    playing(&f);
    if(!cancel) {
        for(unsigned n=0;n<3000;++n) {
            struct kosdec_adx_player_info i;
            CHECK(kosdec_adx_player_get_info(&f.player,&i)==0);
            if(i.state==KOSDEC_ADX_PLAYER_SOURCE_END) break;
            CHECK(kosdec_adx_player_poll(&f.player)==0);
            mock_pause();
        }
    }
    mock_failure=MOCK_JOIN;
    CHECK((cancel?kosdec_adx_player_cancel_source(&f.player):kosdec_adx_player_release_source(&f.player))<0);
    CHECK(diagnostic(&f).code==EIO);
    CHECK(kosdec_adx_player_clear_error(&f.player)==0);
    mock_failure=MOCK_OK;
    CHECK((cancel?kosdec_adx_player_cancel_source(&f.player):kosdec_adx_player_release_source(&f.player))==0);
    CHECK(!diagnostic(&f).code);
    finish(&f);
    CHECK(!diagnostic(&f).code);
}
static void status_error(void) {
    struct fixture f;
    struct observer o={.owner=pthread_self()};
    init(&f,1);
    playing(&f);
    mock_status_failure=true;
    CHECK(kosdec_adx_player_set_error_callback(&f.player,observed,&o)==0);
    CHECK(kosdec_adx_player_stop(&f.player,1000)==0);
    CHECK(diagnostic(&f).code==EPIPE && o.calls==0);
    CHECK(dispatch(&f,&o)==1 && dispatch(&f,&o)==0);
    CHECK(kosdec_adx_player_clear_error(&f.player)==0);
    CHECK(!diagnostic(&f).code);
    finish(&f);
}
static void independent(void) {
    struct fixture a,b;
    init(&a,1); init(&b,2);
    mock_failure=MOCK_CREATE;
    CHECK(kosdec_adx_player_start(&a.player)<0);
    CHECK(diagnostic(&a).code==EIO && !diagnostic(&b).code);
    CHECK(kosdec_adx_player_clear_error(&b.player)==0 && diagnostic(&a).code==EIO);
    finish(&a); finish(&b);
}
int main(void) {
    invalid();
    for(unsigned channels=1;channels<=2;++channels)
        for(enum mock_failure f=MOCK_CREATE;f<=MOCK_CALLBACK;++f) injection(f,channels);
    pending_and_cleanup();
    for(unsigned i=0;i<3;++i) source_fault(i);
    source_join(false); source_join(true); status_error(); independent();
    printf("ADX_PLAYER_ERROR checks=%u failures=0\n",checks);
    return 0;
}
