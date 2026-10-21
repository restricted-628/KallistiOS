#include "test-shim.h"
enum net_shutdown_stage { NET_SHUTDOWN_PLATFORM=1,NET_SHUTDOWN_NETWORK,NET_SHUTDOWN_ETHERNET,NET_SHUTDOWN_HARDWARE };
static bool net_shutdown_runtime_live;
volatile int arch_net_shutdown_error;
volatile enum net_shutdown_stage arch_net_shutdown_stage;
static unsigned serial_calls,sleeps,cpu_sleeps,net_checks,bba_checks;
static int serial_result,net_result,bba_result;
static jmp_buf terminal;
#define DCLOAD_TYPE_SER 1
#define SCIF_FIFO_CAPACITY 16
#define SCFSR_TDFE 0x20
#define SCFSR_TEND 0x40
static bool serial_initialized, serial_enabled, spi_active;
static int dcload_type;
static uint16_t SCFDR2, SCFSR2;
static uint8_t SCFTDR2;
#define scif_try_write raw_scif_try_write
#include "scif.inc"
#undef scif_try_write
static bool actual_serial;
static int scif_try_write(int c) {
    ++serial_calls;
    if(actual_serial) return raw_scif_try_write(c);
    errno=EAGAIN;return serial_result;
}
static void thd_sleep(int ms) { CHECK(ms==1000&&mask==0&&!in_irq);++sleeps;longjmp(terminal,1); }
static void arch_sleep(void) { ++cpu_sleeps;longjmp(terminal,1); }
static void legacy(void) { CHECK(false); }
static void net_shutdown(void) { CHECK(false); }
void eth_shutdown(void);
struct net_shutdown_binding { void (*shutdown)(void); int (*check)(void); };
static struct net_shutdown_binding custom_binding;
static const struct net_shutdown_binding *net_custom,*eth_custom;
#define arch_net_shutdown_custom (*net_custom)
#define arch_eth_shutdown_custom (*eth_custom)
static unsigned custom_checks;
static int custom_result;
static int custom_check(void) { ++custom_checks;errno=EACCES;return custom_result; }
static void (*net_shutdown_weak)(void),(*eth_shutdown_weak)(void);
static int net_check(void) { ++net_checks;errno=EBUSY;return net_result; }
static int bba_check(void) { ++bba_checks;errno=EBUSY;return bba_result; }
static int (*net_quiesce_checked)(void),(*bba_shutdown_preflight)(void);
#define __weak_symbol
#define KOS_INIT_FLAG_CALL(fn) (++destructive, 0)
#define KOS_PLATFORM_IS_NAOMI 0
#define INIT_MALLOCSTATS 1
#define DBG_CRITICAL 0
#define DBG_INFO 0
#define __fallthrough __attribute__((fallthrough))
#define ARCH_EXIT_RETURN 0
#define ARCH_EXIT_MENU 1
#define ARCH_EXIT_REBOOT 2
static unsigned destructive;
static unsigned __kos_init_flags;
static int arch_exit_path,initted;
static void (*fs_shutdown_weak)(void);
#define DESTROY(fn) static void fn(void) { ++destructive; }
DESTROY(_fini) DESTROY(wdt_disable) DESTROY(ubc_shutdown) DESTROY(malloc_stats)
DESTROY(irq_shutdown) DESTROY(fs_fdtbl_destroy) DESTROY(dcload_syscall_net_shutdown)
DESTROY(nmmgr_shutdown) DESTROY(snd_shutdown) DESTROY(pvr_shutdown)
DESTROY(thd_shutdown) DESTROY(timer_shutdown) DESTROY(rtc_shutdown)
DESTROY(arch_menu) DESTROY(arch_reboot) DESTROY(la_shutdown)
DESTROY(w5500_adapter_shutdown) DESTROY(g2_dma_shutdown) DESTROY(spu_shutdown)
DESTROY(vid_shutdown) DESTROY(vblank_shutdown) DESTROY(asic_shutdown)
static int bba_shutdown(void) { ++destructive;return 0; }
static void arch_return(int n) { (void)n;++destructive; }
static void dbglog(int level,const char *s,...) { (void)level;(void)s;++destructive; }
void hardware_shutdown(void);
#include "platform.inc"
#include "hardware.inc"
static void reset(void) {
    mask=0;in_irq=false;current=&main_thread;net_shutdown_runtime_live=true;destructive=0;
    serial_calls=sleeps=cpu_sleeps=net_checks=bba_checks=0;serial_result=1;
    net_result=bba_result=0;net_shutdown_weak=eth_shutdown_weak=NULL;net_custom=eth_custom=NULL;custom_checks=0;custom_result=0;
    custom_binding=(struct net_shutdown_binding){legacy,custom_check};
    net_quiesce_checked=bba_shutdown_preflight=NULL;
    arch_net_shutdown_error=0;arch_net_shutdown_stage=0;
    actual_serial=false;serial_initialized=serial_enabled=true;spi_active=false;
    dcload_type=0;SCFDR2=0;SCFSR2=0x60;SCFTDR2=0;
}
static void wait_once(int error) {
    if(!setjmp(terminal))arch_net_shutdown_wait(error,NET_SHUTDOWN_PLATFORM);
    CHECK(arch_net_shutdown_error==error&&arch_net_shutdown_stage==NET_SHUTDOWN_PLATFORM);
    CHECK(!net_checks&&!bba_checks); /* never retries teardown */
}
int main(void) {
    reset();CHECK(arch_net_shutdown_preflight()==0);
    net_shutdown_weak=net_shutdown;CHECK(arch_net_shutdown_preflight()<0&&errno==ENOSYS);
    net_quiesce_checked=net_check;CHECK(arch_net_shutdown_preflight()==0&&net_checks==1);
    eth_shutdown_weak=eth_shutdown;CHECK(arch_net_shutdown_preflight()<0&&errno==ENOSYS&&net_checks==1);
    bba_shutdown_preflight=bba_check;net_result=-1;CHECK(arch_net_shutdown_preflight()<0&&!bba_checks);
    net_result=0;bba_result=-1;CHECK(arch_net_shutdown_preflight()<0&&bba_checks==1);
    reset();net_quiesce_checked=net_check;bba_shutdown_preflight=bba_check;CHECK(arch_net_shutdown_preflight()==0&&net_checks==1&&bba_checks==1); /* manual init/off */
    reset();net_quiesce_checked=net_check;bba_shutdown_preflight=bba_check;
    net_shutdown_weak=legacy;CHECK(arch_net_shutdown_preflight()<0&&errno==ENOSYS&&!net_checks);
    net_custom=&custom_binding;custom_result=-1;CHECK(arch_net_shutdown_preflight()<0&&errno==EACCES&&custom_checks==1&&!net_checks);
    custom_result=0;CHECK(arch_net_shutdown_preflight()==0&&net_checks==1&&bba_checks==1);
    custom_binding.shutdown=net_shutdown;CHECK(arch_net_shutdown_preflight()<0&&errno==ENOSYS);
    reset();net_quiesce_checked=net_check;bba_shutdown_preflight=bba_check;
    eth_shutdown_weak=legacy;CHECK(arch_net_shutdown_preflight()<0&&errno==ENOSYS&&!net_checks);
    eth_custom=&custom_binding;custom_result=-1;CHECK(arch_net_shutdown_preflight()<0&&errno==EACCES&&!net_checks);
    custom_result=0;CHECK(arch_net_shutdown_preflight()==0&&net_checks==1&&bba_checks==1);
    reset();wait_once(EBUSY);CHECK(sleeps==1&&!cpu_sleeps&&serial_calls>0&&serial_calls<100);
    reset();serial_result=0;wait_once(EBUSY);CHECK(serial_calls==1&&sleeps==1);
    reset();serial_result=-1;wait_once(EDEADLK);CHECK(serial_calls==1&&sleeps==1); /* ordinary self-stop */
    for(unsigned reason=0;reason<5;++reason) {
        reset();actual_serial=true;
        switch(reason) {
            case 0: serial_initialized=false; break;
            case 1: serial_enabled=false; break;
            case 2: spi_active=true; break;
            case 3: dcload_type=DCLOAD_TYPE_SER; break;
            case 4: SCFDR2=16<<8; break;
        }
        wait_once(EBUSY);CHECK(serial_calls==1&&sleeps==1&&SCFTDR2==0&&SCFSR2==0x60);
    }
    reset();actual_serial=true;wait_once(EBUSY);CHECK(serial_calls>1&&serial_calls<100&&sleeps==1&&SCFTDR2=='\n');
    reset();in_irq=true;wait_once(EPERM);CHECK(cpu_sleeps==1&&!sleeps&&!serial_calls&&mask==0);
    reset();mask=0x100000f0;wait_once(EPERM);CHECK(cpu_sleeps==1&&!serial_calls&&mask==0x100000f0);
    reset();net_shutdown_runtime_live=false;wait_once(EPERM);CHECK(cpu_sleeps==1&&!serial_calls);
    reset();current=NULL;wait_once(EPERM);CHECK(cpu_sleeps==1&&!serial_calls);
    reset();current=&worker_thread;worker_thread.state=0;wait_once(EPERM);CHECK(cpu_sleeps==1&&!serial_calls);
    void (*entries[])(void)={arch_shutdown,arch_auto_shutdown,eth_shutdown,hardware_shutdown};
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);++i) {
        reset();net_quiesce_checked=net_check;net_result=-1;
        if(!setjmp(terminal))entries[i]();
        CHECK(destructive==0&&sleeps==1&&net_checks==1);
    }
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);++i) {
        reset();net_quiesce_checked=net_check;bba_shutdown_preflight=bba_check;
        net_shutdown_weak=legacy;
        if(!setjmp(terminal))entries[i]();
        CHECK(!destructive&&sleeps==1&&!net_checks&&arch_net_shutdown_error==ENOSYS);
        reset();net_quiesce_checked=net_check;bba_shutdown_preflight=bba_check;
        net_shutdown_weak=legacy;net_custom=&custom_binding;custom_result=-1;
        if(!setjmp(terminal))entries[i]();
        CHECK(!destructive&&sleeps==1&&!net_checks&&custom_checks==1&&arch_net_shutdown_error==EACCES);
    }
    reset();net_quiesce_checked=net_check;net_result=-1;
    if(!setjmp(terminal))arch_exit_handler(0);
    CHECK(destructive==0&&sleeps==1&&net_checks==1);
    puts("platform: typed dispatch, persistent reason, bounded diagnostic and both terminal branches passed");
}
