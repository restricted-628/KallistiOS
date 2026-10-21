#include "test-shim.h"
#define NETIF_RUNNING 1u
#define NETIF_INITIALIZED 2u
typedef struct netif netif_t;
struct netif { unsigned flags; int (*if_stop)(netif_t *); int (*if_shutdown)(netif_t *); netif_t *next; uint8_t ip_addr[4]; };
#define LIST_FOREACH(it,head,field) for((it)=*(head);it;(it)=(it)->next)
typedef struct { kthread_t *thread; } workqueue_t;
static netif_t dev,*net_if_list,*net_default_dev;
static bool dev_init_fails, queue_create_fails;
static unsigned initializers;
static workqueue_t queue,*net_wq;
static int net_initted;
static bool net_closing,net_quiesced,net_shutdown_busy;
static bool self_input,join_fails,stop_fails,release_fails,final_held;
static unsigned stopped,killed,destroyed,consumers,default_cleared,released;
static bool arch_net_shutdown_can_wait(void) { return !mask&&!in_irq&&current; }
static kthread_t *workqueue_get_thread(workqueue_t *w) { CHECK(!mask);return w->thread; }
static void workqueue_kill(workqueue_t *w) { ++killed;if(!join_fails)w->thread=NULL; }
static void workqueue_destroy(workqueue_t *w) { CHECK(!w->thread);++destroyed; }
static int context_check(void) { if(self_input){errno=EDEADLK;return -1;}return 0; }
static int final_check(void) { if(final_held){errno=EBUSY;return -1;}return 0; }
static int (*bba_shutdown_context_check)(void)=context_check;
static int (*bba_shutdown_preflight)(void)=final_check;
static int stop(netif_t *d) { ++stopped;if(stop_fails){errno=EBUSY;return -1;}d->flags&=~NETIF_RUNNING;return 0; }
static int release(netif_t *d) { ++released;if(release_fails){errno=EBUSY;return -1;}d->flags&=~NETIF_INITIALIZED;return 0; }
#define DESTROY(name) static void name(void) { ++consumers; }
DESTROY(net_dhcp_shutdown) DESTROY(net_tcp_shutdown) DESTROY(net_udp_shutdown)
DESTROY(fs_socket_shutdown) DESTROY(net_ipv6_shutdown) DESTROY(net_multicast_shutdown)
DESTROY(net_ipv4_frag_shutdown) DESTROY(net_ndp_shutdown) DESTROY(net_arp_shutdown)
static void net_set_default(void *p) { CHECK(!p);++default_cleared; }
#define INITIALIZE(name) static void name(void) { ++initializers; }
INITIALIZE(net_arp_init) INITIALIZE(net_ndp_init) INITIALIZE(net_ipv4_frag_init)
INITIALIZE(net_multicast_init) INITIALIZE(net_ipv6_init) INITIALIZE(fs_socket_init)
INITIALIZE(net_udp_init) INITIALIZE(net_tcp_init) INITIALIZE(net_dhcp_init)
int net_quiesce_checked(void);
static int net_dev_init(void) {
    CHECK(net_quiesce_checked()<0&&errno==EBUSY&&!net_closing);
    return dev_init_fails?-1:0;
}
static workqueue_t *workqueue_create(void) { return queue_create_fails?NULL:&queue; }
static int net_dhcp_request(uint32_t ip) { (void)ip;return 0; }
static void net_ipv4_parse_address(uint32_t ip,uint8_t *out) { (void)ip;(void)out; }
#define DBG_DEBUG 0
static void dbglog(int level,const char *s) { (void)level;(void)s; }
#define NET_SHUTDOWN_NETWORK 2
static jmp_buf terminal;
static void arch_net_shutdown_wait(int error,int stage) { CHECK(error==EBUSY&&stage==2);longjmp(terminal,1); }
#include "network.inc"
static void reset(void) {
    mask=0;in_irq=false;current=&main_thread;
    dev=(netif_t){.flags=NETIF_RUNNING|NETIF_INITIALIZED,.if_stop=stop,.if_shutdown=release};net_if_list=&dev;
    queue.thread=&worker_thread;net_wq=&queue;net_initted=1;
    net_closing=net_quiesced=net_shutdown_busy=false;
    self_input=join_fails=stop_fails=release_fails=final_held=false;
    stopped=killed=destroyed=consumers=default_cleared=released=initializers=0;
    dev_init_fails=queue_create_fails=false;net_default_dev=NULL;
}
int main(void) {
    reset();stop_fails=true;CHECK(net_shutdown_checked()<0);CHECK(net_closing&&!net_quiesced&&!killed&&!consumers&&!destroyed&&net_initted);
    stop_fails=false;CHECK(net_quiesce_checked()==0);CHECK(stopped==2&&killed==1&&!consumers);
    CHECK(net_quiesce_checked()==0&&killed==1);final_held=true;CHECK(net_shutdown_checked()<0&&!released&&!consumers&&!destroyed);
    final_held=false;CHECK(net_shutdown_checked()==0&&consumers==9&&destroyed==1&&net_wq==NULL&&!net_initted);
    CHECK(net_shutdown_checked()==0&&consumers==9&&destroyed==1);
    reset();join_fails=true;CHECK(net_quiesce_checked()<0&&!net_quiesced&&!consumers);join_fails=false;CHECK(net_quiesce_checked()==0&&killed==2);
    reset();release_fails=true;CHECK(net_shutdown_checked()<0&&!consumers&&!destroyed&&net_initted);
    reset();current=&worker_thread;CHECK(net_quiesce_checked()<0&&errno==EDEADLK&&!net_closing&&!stopped&&!killed);
    reset();self_input=true;CHECK(net_quiesce_checked()<0&&!net_closing&&!net_shutdown_busy);
    reset();mask=0xf0;CHECK(net_quiesce_checked()<0&&!net_closing);mask=0;in_irq=true;CHECK(net_quiesce_checked()<0&&!net_closing);
    reset();net_shutdown_busy=true;CHECK(net_quiesce_checked()<0&&!net_closing&&!stopped);
    reset();final_held=true;if(!setjmp(terminal)){net_shutdown();CHECK(false);}CHECK(!consumers&&!destroyed&&net_initted);
    reset();net_initted=0;net_wq=NULL;final_held=true;CHECK(net_shutdown_checked()<0&&!consumers); /* partial init */
    CHECK(net_shutdown_admission_closed());
    reset();net_initted=0;net_wq=NULL;dev_init_fails=true;
    CHECK(net_init(0)<0&&!net_shutdown_busy&&!initializers);
    dev_init_fails=false;queue_create_fails=true;CHECK(net_init(0)<0&&!net_shutdown_busy&&!initializers);
    queue_create_fails=false;CHECK(net_init(0)==0&&net_initted&&initializers==9);
    CHECK(net_init(0)==0&&initializers==9);CHECK(net_quiesce_checked()==0);
    CHECK(net_init(0)<0&&errno==EBUSY&&net_closing&&initializers==9);
    puts("network: refusal, join proof, self/context rejection and retry passed");
}
