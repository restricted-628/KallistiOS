#include "test-shim.h"
#define NETIF_RUNNING 1u
#define NETIF_INITIALIZED 2u
#define NETIF_REGISTERED 4u
#define NETIF_NO_FLAGS 0u
#define DBG_KDEBUG 0
#define ASIC_EVT_CLAIM_INVALID 0
#define GAPS_OWNER_INVALID 0
#define G2_DMA_CHAN_BBA 1
#define G2_DMA_STATE_IDLE 0
#define G2_DMA_STATE_RUNNING 1
#define G2_DMA_STATE_COMPLETE 2
#define G2_DMA_STATE_CANCELLED 3
#define G2_DMA_TO_SH4 0
#define MAX_PKTS 8
#define RX_BUFFER_LEN 16384
#define NIC(x) (x)
#define RT_RXBUFHEAD 1
#define RT_RXBUFTAIL 2
#define RT_CHIPCMD 3
#define RT_RXCONFIG 4
#define RT_INTRSTATUS 5
#define RT_CMD_TX_ENABLE 1
#define RT_CMD_RX_ENABLE 2
#define RX_CONFIG_DEFAULT 0
#define RT_RXC_APM 1
#define RT_RXC_AB 2
#define DBG_ERROR 1
#define DBG_INFO 2
#define RT_MII_BMSR 6
#define RT_MII_BMCR 7
#define RT_MII_LINK 1
#define RT_MII_RESET 2
#define RT_MII_AN_ENABLE 4
#define RT_MII_AN_START 8
#define RT_INT_RX_ACK 1
#define RT_INT_TX_OK 2
#define RT_INT_LINK_CHANGE 4
#define RT_INT_RXBUF_OVERFLOW 8
#define BBA_TX_ERROR -1
#define BBA_TX_AGAIN -2
#define __is_defined(x) 1

typedef struct netif netif_t;
struct netif {
    unsigned flags;
    uint8_t mac_addr[6], ip_addr[4], netmask[4], gateway[4], broadcast[4], dns[4];
    const char *name, *descr;
    unsigned index, dev_id, mtu, ip6_addr_count, mtu6, hop_limit;
    uint8_t ip6_lladdr[16], ip6_gateway[16];
    void *ip6_addrs;
    int (*if_detect)(netif_t *), (*if_init)(netif_t *), (*if_shutdown)(netif_t *);
    int (*if_start)(netif_t *), (*if_stop)(netif_t *), (*if_tx_commit)(netif_t *);
    int (*if_rx_poll)(netif_t *);
    int (*if_tx)(netif_t *, const uint8_t *, int, int);
    int (*if_set_flags)(netif_t *, uint32_t, uint32_t);
    int (*if_set_mc)(netif_t *, const uint8_t *, int);
};
typedef struct { kthread_t *thread; bool live; } kthread_worker_t;
typedef struct { int state, result; bool callback_pending; uint64_t sequence; } g2_dma_status_t;
static netif_t bba_if;
static bool bba_hw_retained, bba_global_closing, bba_lifecycle_busy;
static enum { BBA_STOPPED, BBA_STARTING, BBA_RUNNING, BBA_STOPPING } bba_stage;
static unsigned tx_users, rx_users;
static kthread_t *input_owner;
static bool dma_seen,dma_known;
static uint64_t dma_sequence;
static atomic_bool bba_stopping;
static atomic_int dma_used;
static int next_len;
static uint8_t *next_dst,*next_src;
static int bba_irq_claim,gaps_owner,tx_sema;
static bool link_stable, tx_sema_initialized, detect_fail, registration_race;
static unsigned sem_inits, sem_destroys, registrations;
static int rxin,rxout;
static uint32_t rx_size;
static struct { unsigned cur_rx; uint8_t mac[6]; } rtl;
static struct { uint8_t *rxbuff; int offt,pkt_size; } rx_pkt[MAX_PKTS];
static kthread_worker_t worker,*rx_worker;
static int bba_rx_worker_attr;
static void (*eth_rx_callback)(uint8_t *,int);
static bool closing, status_fail, create_fail, poll_fail, hw_init_fail, alloc_fail;
static unsigned destroys,wakes,writes,chains,rx_calls,hw_calls,freed,delivered;
static unsigned irq_status,link_status,phy_writes,irq_acks;
static void (*during_status)(void);
static g2_dma_status_t dma_status;
static bool is_closed(void) { return closing; }
static bool (*net_shutdown_admission_closed)(void)=is_closed;
static bool arch_net_shutdown_can_wait(void) { return !in_irq && !mask && current && current->state==STATE_RUNNING; }
static kthread_t *thd_worker_get_thread(kthread_worker_t *w) { CHECK(w&&w->live); return w->thread; }
static void (*during_join)(void), (*after_worker_free)(void);
static void thd_worker_destroy(kthread_worker_t *w) {
    CHECK(!mask && current!=w->thread && w->live); ++destroys;
    if(during_join) during_join();
    w->live=false;
    if(after_worker_free) after_worker_free();
}
static void thd_worker_wakeup(kthread_worker_t *w) { CHECK(w&&w->live); ++wakes; }
static void thd_schedule(bool force) { (void)force; }
static kthread_worker_t *thd_worker_create_ex(void *a,void (*fn)(void *),void *d) {
    (void)a;(void)fn;(void)d; if(create_fail)return NULL;
    worker=(kthread_worker_t){&worker_thread,true}; return &worker;
}
static bool thd_poll(int (*fn)(void *),void *d,int timeout) { (void)timeout; return !poll_fail && fn(d); }
static int g2_dma_get_status(unsigned ch,g2_dma_status_t *s) {
    CHECK(ch==G2_DMA_CHAN_BBA); if(status_fail){errno=ENODEV;memset(s,0,sizeof(*s));return -1;} *s=dma_status;return 0;
}
static int g2_dma_transfer(void *d,void *s,int n,int b,void (*cb)(void *),void *p,int dir,int mode,int ch,int sh) {
    (void)d;(void)s;(void)n;(void)b;(void)cb;(void)p;(void)dir;(void)mode;(void)ch;(void)sh;
    ++chains;dma_status.state=G2_DMA_STATE_RUNNING;dma_status.sequence++;return 0;
}
static unsigned g2_read_16(unsigned a) {
    if(a==RT_INTRSTATUS&&during_status) { void (*f)(void)=during_status;during_status=NULL;f(); }
    return a==RT_INTRSTATUS?irq_status:a==RT_MII_BMSR?link_status:0;
}
static unsigned g2_read_8(unsigned a) { (void)a;return RT_CMD_RX_ENABLE; }
static void g2_write_16(unsigned a,unsigned v) { (void)v;++writes;if(a==RT_MII_BMCR)++phy_writes;if(a==RT_INTRSTATUS)++irq_acks; }
static void g2_write_8(unsigned a,unsigned v) { (void)a;(void)v;++writes; }
static void g2_write_32(unsigned a,unsigned v) { (void)a;(void)v;++writes; }
static void dbglog(int level,const char *s,...) { (void)level;(void)s; }
static void bba_rx_inner(void) { CHECK(mask&&rx_users);++rx_calls; }
static int bba_hw_init(void) { ++hw_calls;bba_hw_retained=true;gaps_owner=1;return hw_init_fail?-1:0; }
static int bba_hw_shutdown(void) { ++hw_calls;return -1; }
static int bba_rxbuf_alloc(void) { return alloc_fail?-1:0; }
static void bba_rxbuf_free(void) { ++freed; }
static void set_ipv6_lladdr(void) { }
static int sem_wait_irqsafe(int *s) { (void)s;return 0; }
static void sem_signal(int *s) { (void)s; }
static int bba_rtx(const uint8_t *p,int n,int wait) { (void)p;(void)n;(void)wait;return 0; }
static int sem_init(int *s,int n) { (void)s;CHECK(n==1);++sem_inits;return 0; }
static void sem_destroy(int *s) { (void)s;++sem_destroys; }
static void bba_if_netinput(uint8_t *p,int n) { (void)p;(void)n; }
static void bba_set_rx_callback(void (*cb)(uint8_t *,int)) { eth_rx_callback=cb; }
static void bba_get_mac(uint8_t *p) { memcpy(p,rtl.mac,6); }
static int bba_if_detect(netif_t *d) { (void)d;return detect_fail?-1:0; }
int bba_shutdown_preflight(void);
static void bba_set_ispcfg(void) {
    if(registration_race) CHECK(bba_shutdown_preflight()<0&&bba_lifecycle_busy);
}
static int net_reg_device(netif_t *d) { ++registrations;if(closing)return -1;d->flags|=NETIF_REGISTERED;return 0; }
static int bba_if_tx(netif_t *d,const uint8_t *p,int n,int w) { (void)d;(void)p;(void)n;(void)w;return 0; }
static int bba_if_tx_commit(netif_t *d) { (void)d;return 0; }
static int bba_if_set_flags(netif_t *d,uint32_t a,uint32_t o) { (void)d;(void)a;(void)o;return 0; }
static int bba_if_set_mc(netif_t *d,const uint8_t *p,int n) { (void)d;(void)p;(void)n;return 0; }
int bba_shutdown_preflight(void);
#include "driver.inc"

static uint8_t packet[32];
static kthread_t other_thread={STATE_RUNNING};
static void input(uint8_t *p,int n) { CHECK(p==packet&&n==12);++delivered; }
static void reset(void) {
    mask=0;in_irq=false;current=&main_thread;bba_if.flags=NETIF_INITIALIZED|NETIF_RUNNING;
    bba_hw_retained=true;bba_global_closing=bba_lifecycle_busy=false;bba_stage=BBA_RUNNING;
    tx_users=rx_users=0;input_owner=NULL;dma_seen=dma_known=false;dma_sequence=0;
    bba_stopping=false;dma_used=0;next_len=0;next_dst=next_src=packet;
    bba_irq_claim=gaps_owner=1;link_stable=true;rxin=1;rxout=0;rx_size=12;
    rx_pkt[0].rxbuff=packet;rx_pkt[0].offt=0;rx_pkt[0].pkt_size=12;
    worker=(kthread_worker_t){&worker_thread,true};rx_worker=&worker;
    eth_rx_callback=input;closing=status_fail=create_fail=poll_fail=hw_init_fail=alloc_fail=false;
    destroys=wakes=writes=chains=rx_calls=hw_calls=freed=delivered=0;
    dma_status=(g2_dma_status_t){.state=G2_DMA_STATE_IDLE};during_join=after_worker_free=NULL;
    irq_status=phy_writes=irq_acks=0;during_status=NULL;link_status=RT_MII_LINK;
    tx_sema_initialized=detect_fail=registration_race=false;sem_inits=sem_destroys=registrations=0;
}
static void expect_busy(void) { CHECK(bba_if_stop(&bba_if)==-1);CHECK(destroys==0&&rx_worker==&worker&&worker.live);CHECK(bba_stopping); }
static void poll_self(uint8_t *p,int n) {
    input(p,n); CHECK(bba_if_stop(&bba_if)==-1&&errno==EDEADLK);CHECK(!bba_stopping);
    CHECK(!bba_rx_process()); /* Recursive poll cannot borrow the same queue item. */
}
static void close_during_delivery(uint8_t *p,int n) {
    input(p,n);current=&other_thread;expect_busy();current=&main_thread;
}
static void close_during_poll(void) {
    CHECK(rx_users==1&&!mask);current=&other_thread;expect_busy();
    CHECK(bba_if_start(&bba_if)<0);current=&main_thread;
}
static void concurrent_stop_after_free(void) {
    CHECK(bba_shutdown_context_check()<0&&errno==EBUSY);
    CHECK(bba_if_stop(&bba_if)<0&&errno==EBUSY);
}
static void worker_returns(void) { CHECK(input_owner==&worker_thread);input_owner=NULL; }
int main(void) {
    reset();dma_used=1;next_len=64;dma_seen=dma_known=true;dma_sequence=3;
    dma_status=(g2_dma_status_t){G2_DMA_STATE_RUNNING,0,true,3};expect_busy();
    bba_dma_cb(NULL);CHECK(!next_len&&!dma_used&&!wakes&&!chains&&!rx_calls);
    dma_status.state=G2_DMA_STATE_COMPLETE;expect_busy(); /* callback still pending */
    dma_status.callback_pending=false;CHECK(bba_if_stop(&bba_if)==0&&destroys==1&&rx_worker==NULL);
    CHECK(bba_hw_retained&&gaps_owner==1&&!writes);CHECK(bba_if_start(&bba_if)==0&&!bba_stopping);
    reset();after_worker_free=concurrent_stop_after_free;CHECK(bba_if_stop(&bba_if)==0);
    reset();bba_stopping=true;rx_finish_enq(1);rx_reset();CHECK(!wakes&&!writes);
    reset();tx_users=1;expect_busy();CHECK(!writes);reset();rx_users=1;expect_busy();
    reset();status_fail=true;expect_busy();CHECK(errno==ENODEV);
    reset();dma_seen=true;dma_known=false;expect_busy();
    reset();dma_seen=dma_known=true;dma_sequence=9;dma_status=(g2_dma_status_t){G2_DMA_STATE_COMPLETE,0,false,8};expect_busy();
    dma_status.sequence=9;dma_status.state=G2_DMA_STATE_CANCELLED;expect_busy();
    dma_status.state=G2_DMA_STATE_COMPLETE;dma_status.result=EIO;expect_busy();
    reset();bba_lifecycle_busy=true;CHECK(bba_if_stop(&bba_if)<0&&!bba_stopping&&!destroys);
    reset();in_irq=true;CHECK(bba_if_stop(&bba_if)<0&&!bba_stopping);in_irq=false;mask=0xf0;CHECK(bba_if_stop(&bba_if)<0&&!bba_stopping);
    reset();current=&worker_thread;CHECK(bba_if_stop(&bba_if)<0&&errno==EDEADLK&&!bba_stopping);
    reset();eth_rx_callback=poll_self;CHECK(bba_rx_process()&&delivered==1&&!input_owner&&rxout==1);
    reset();eth_rx_callback=close_during_delivery;CHECK(bba_rx_process()&&delivered==1&&!input_owner);CHECK(bba_if_stop(&bba_if)==0);
    reset();input_owner=&worker_thread;during_join=worker_returns;CHECK(bba_if_stop(&bba_if)==0&&!input_owner);
    reset();CHECK(bba_shutdown_preflight()<0&&errno==EBUSY&&bba_global_closing);CHECK(!writes&&!freed&&!hw_calls);
    CHECK(bba_if_shutdown(&bba_if)<0&&!hw_calls&&!freed);CHECK(bba_if_start(&bba_if)<0);
    reset();bba_if.flags=0;rx_worker=NULL;CHECK(bba_shutdown_preflight()<0&&gaps_owner==1&&!hw_calls);
    reset();bba_hw_retained=false;gaps_owner=0;rx_worker=NULL;bba_if.flags=0;CHECK(bba_shutdown_preflight()==0);
    reset();CHECK(bba_if_stop(&bba_if)==0);create_fail=true;CHECK(bba_if_start(&bba_if)<0&&bba_stopping&&!rx_worker);create_fail=false;CHECK(bba_if_start(&bba_if)==0);
    reset();CHECK(bba_if_stop(&bba_if)==0);poll_fail=true;CHECK(bba_if_start(&bba_if)<0&&bba_stopping&&!rx_worker);
    reset();bba_if.flags=0;rx_worker=NULL;alloc_fail=true;CHECK(bba_if_init(&bba_if)<0&&bba_hw_retained&&!bba_lifecycle_busy&&!freed);
    reset();bba_stopping=true;CHECK(bba_tx(packet,12,0)==BBA_TX_ERROR&&tx_users==0);
    reset();current=&worker_thread;bba_rx_worker(NULL);CHECK(delivered==1&&rxout==rxin);
    reset();CHECK(bba_init()<0&&rx_worker==&worker&&!sem_inits); /* live registration retained */
    reset();bba_hw_retained=false;gaps_owner=0;rx_worker=NULL;bba_if.flags=0;
    detect_fail=true;CHECK(bba_init()<0&&sem_inits==1&&!bba_lifecycle_busy);
    detect_fail=false;CHECK(bba_init()==0&&sem_inits==1&&registrations==1);
    CHECK(bba_init()==0&&registrations==1);CHECK(bba_shutdown()==0&&sem_destroys==1);
    CHECK(bba_shutdown()==0&&sem_destroys==1);CHECK(bba_init()<0&&sem_inits==1);
    reset();bba_hw_retained=false;gaps_owner=0;rx_worker=NULL;bba_if.flags=0;
    registration_race=true;CHECK(bba_init()==0&&!bba_global_closing);CHECK(bba_init()==0&&registrations==1);
    reset();during_status=close_during_poll;irq_status=RT_INT_RX_ACK;
    CHECK(bba_if_rx_poll(&bba_if)==0&&rx_users==0&&!delivered&&irq_acks==1&&!destroys);
    CHECK(bba_if_stop(&bba_if)==0);CHECK(bba_if_start(&bba_if)==0);
    reset();eth_rx_callback=close_during_delivery;irq_status=RT_INT_RX_ACK;
    CHECK(bba_if_rx_poll(&bba_if)==0&&rx_users==0&&delivered==1);CHECK(bba_if_stop(&bba_if)==0);
    reset();bba_stopping=true;irq_status=RT_INT_RX_ACK;CHECK(bba_if_rx_poll(&bba_if)==0&&!writes&&!rx_calls&&!delivered);
    for(unsigned mode=0;mode<5;++mode) {
        reset();link_status=0;irq_status=RT_INT_LINK_CHANGE|RT_INT_RX_ACK;
        if(mode==0){bba_stage=BBA_STOPPING;bba_stopping=true;}
        if(mode==1){CHECK(bba_if_stop(&bba_if)==0);}
        if(mode==2){closing=true;}
        if(mode==3){bba_global_closing=true;bba_stage=BBA_STARTING;}
        if(mode==4){tx_users=1;expect_busy();}
        bba_irq_hnd(0,NULL);CHECK(!phy_writes&&irq_acks==2);
    }
    reset();bba_stage=BBA_STARTING;bba_stopping=true;link_status=0;irq_status=RT_INT_LINK_CHANGE;
    bba_irq_hnd(0,NULL);CHECK(phy_writes==1&&!link_stable&&irq_acks==1);
    link_status=RT_MII_LINK;bba_irq_hnd(0,NULL);CHECK(phy_writes==1&&link_stable);
    reset();link_status=0;irq_status=RT_INT_LINK_CHANGE;bba_irq_hnd(0,NULL);CHECK(phy_writes==1&&!link_stable);
    reset();CHECK(bba_if_stop(&bba_if)==0);link_status=0; /* stale prior link must not reopen */
    CHECK(bba_if_start(&bba_if)<0&&bba_stopping&&phy_writes==1&&!link_stable);
    puts("driver: retirement, callback, poll, startup and retained-resource cases passed");
}
