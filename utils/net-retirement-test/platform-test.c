#include "test-shim.h"
enum net_shutdown_stage { NET_SHUTDOWN_PLATFORM=1,NET_SHUTDOWN_NETWORK,NET_SHUTDOWN_ETHERNET,NET_SHUTDOWN_HARDWARE };
static bool net_shutdown_runtime_live;
volatile int arch_net_shutdown_error;
volatile enum net_shutdown_stage arch_net_shutdown_stage;
static volatile int arch_wave_shutdown_error;
static kthread_t *platform_shutdown_owner, *wave_owner;
static bool platform_shutdown_started, auto_shutdown_started, hardware_shutdown_active;
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
#ifndef __noreturn
#define __noreturn __attribute__((noreturn))
#endif
#define KOS_INIT_FLAG_CALL(fn) (++destructive, 0)
#define KOS_PLATFORM_IS_NAOMI 0
#define INIT_MALLOCSTATS 1
#define DBG_CRITICAL 0
#define DBG_INFO 0
#define __fallthrough __attribute__((fallthrough))
#define ARCH_EXIT_RETURN 0
#define ARCH_EXIT_MENU 1
#define ARCH_EXIT_REBOOT 2
#define DEFAULT_VID_MODE 0
#define DEFAULT_PIXEL_MODE 0
static unsigned destructive;
static unsigned __kos_init_flags;
static int arch_exit_path,initted;
static void (*fs_shutdown_weak)(void);
static unsigned idle_reservations, active_writers;
static bool check_wave_steps;
static unsigned wave_step, drain_step, g2_step, sound_step, clear_step, boot_calls;
static int spu_error, g2_error;

static int snd_mem_reconfigure_begin(void) {
    if(mask || in_irq || !current) { errno = EPERM; return -1; }
    if(idle_reservations || active_writers || wave_owner) { errno = EBUSY; return -1; }
    wave_owner = current;
    return 0;
}

static int snd_mem_reconfigure_require_owner(void) {
    if(mask || in_irq || !current || wave_owner != current) { errno = EPERM; return -1; }
    return 0;
}

static int snd_mem_reconfigure_end(void) {
    if(snd_mem_reconfigure_require_owner() < 0)
        return -1;
    wave_owner = NULL;
    return 0;
}

static void destructive_step(void) {
    if(check_wave_steps)
        CHECK(current && wave_owner == current);
    ++destructive;
}

static int spu_transfer_system_shutdown(void) {
    if(spu_error) { errno = spu_error; return -1; }
    destructive_step();
    drain_step = ++wave_step;
    return 0;
}

static int g2_dma_shutdown_checked(void) {
    CHECK(drain_step);
    if(g2_error) { errno = g2_error; return -1; }
    destructive_step();
    g2_step = ++wave_step;
    return 0;
}

static int spu_sound_shutdown_in_transition(void) {
    if(snd_mem_reconfigure_require_owner() < 0)
        return -1;
    destructive_step();
    sound_step = ++wave_step;
    return 0;
}

static void spu_clear_retired(void) {
    CHECK(sound_step && g2_step && drain_step);
    destructive_step();
    clear_step = ++wave_step;
}

static void spu_init_boot(void) {
    CHECK(mask == 0xf0 && !wave_owner);
    ++boot_calls;
}

static int spu_init_in_transition(void) {
    return snd_mem_reconfigure_require_owner();
}

static void g2_dma_init(void) {}
static void vid_init(int mode, int pixel) { CHECK(!mode && !pixel); }
#define DESTROY(fn) static void fn(void) { destructive_step(); }
DESTROY(_fini) DESTROY(wdt_disable) DESTROY(ubc_shutdown) DESTROY(malloc_stats)
DESTROY(irq_shutdown) DESTROY(fs_fdtbl_destroy) DESTROY(dcload_syscall_net_shutdown)
DESTROY(nmmgr_shutdown) DESTROY(pvr_shutdown)
DESTROY(thd_shutdown) DESTROY(timer_shutdown) DESTROY(rtc_shutdown)
DESTROY(arch_menu) DESTROY(arch_reboot) DESTROY(la_shutdown)
DESTROY(w5500_adapter_shutdown)
DESTROY(vid_shutdown) DESTROY(vblank_shutdown) DESTROY(asic_shutdown)
static int bba_shutdown(void) { ++destructive;return 0; }
static void arch_return(int n) { (void)n;++destructive; }
static void dbglog(int level,const char *s,...) { (void)level;(void)s;++destructive; }
void hardware_shutdown(void);
void hardware_shutdown_after_spu_retired(void);
void arch_wave_shutdown_wait(int error) __noreturn;
static void arch_shutdown_wait(const char *message) __noreturn;
#include "platform.inc"
#include "hardware.inc"

static unsigned exit_steps;
static void (*kos_shutdown_weak)(void);
static void __call_exitprocs(int code, void *dso) {
    CHECK(code == 9 && !dso && arch_shutdown_owns_wave_gate() && exit_steps++ == 0);
    destructive_step();
}
static void close_stdio(void) {
    CHECK(arch_shutdown_owns_wave_gate() && exit_steps++ == 1);
    destructive_step();
}
static void (*__stdio_exit_handler)(void) = close_stdio;
static void platform_libc_terminate(int code) __noreturn;
static void platform_libc_terminate(int code) {
    CHECK(code == 9 && exit_steps++ == 2);
    arch_exit_handler(code);
    longjmp(terminal, 1);
}
#define exit platform_libc_exit
#define _exit platform_libc_terminate
#include "exit.inc"
#undef _exit
#undef exit

static void reset(void) {
    mask=0;in_irq=false;current=&main_thread;net_shutdown_runtime_live=true;destructive=0;
    worker_thread.state=STATE_RUNNING;
    serial_calls=sleeps=cpu_sleeps=net_checks=bba_checks=0;serial_result=1;
    net_result=bba_result=0;net_shutdown_weak=eth_shutdown_weak=NULL;net_custom=eth_custom=NULL;custom_checks=0;custom_result=0;
    custom_binding=(struct net_shutdown_binding){legacy,custom_check};
    net_quiesce_checked=bba_shutdown_preflight=NULL;
    arch_net_shutdown_error=0;arch_net_shutdown_stage=0;
    arch_wave_shutdown_error=0;platform_shutdown_owner=wave_owner=NULL;
    platform_shutdown_started=auto_shutdown_started=hardware_shutdown_active=false;
    idle_reservations=active_writers=0;check_wave_steps=false;
    wave_step=drain_step=g2_step=sound_step=clear_step=boot_calls=0;
    spu_error=g2_error=0;initted=2;exit_steps=0;kos_shutdown_weak=legacy;
    actual_serial=false;serial_initialized=serial_enabled=true;spi_active=false;
    dcload_type=0;SCFDR2=0;SCFSR2=0x60;SCFTDR2=0;
}
static void wait_once(int error) {
    if(!setjmp(terminal))arch_net_shutdown_wait(error,NET_SHUTDOWN_PLATFORM);
    CHECK(arch_net_shutdown_error==error&&arch_net_shutdown_stage==NET_SHUTDOWN_PLATFORM);
    CHECK(!net_checks&&!bba_checks); /* never retries teardown */
}

static void exit_with_callbacks(void) { platform_libc_exit(9); }

static void check_wave_platform(void) {
    void (*entries[])(void) = {
        arch_shutdown, arch_auto_shutdown, hardware_shutdown, exit_with_callbacks
    };
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);++i) {
        for(unsigned writing=0;writing<2;++writing) {
            reset();
            idle_reservations = !writing;
            active_writers = writing;
            net_quiesce_checked = net_check;
            if(!setjmp(terminal)) entries[i]();
            CHECK(arch_wave_shutdown_error == EBUSY && sleeps == 1);
            CHECK(!destructive && !net_checks && !wave_owner && !exit_steps);
            CHECK(idle_reservations == !writing && active_writers == writing);
        }
    }

    reset(); active_writers = 1;
    if(!setjmp(terminal)) arch_exit_handler(0);
    CHECK(arch_wave_shutdown_error == EBUSY && !destructive && !net_checks);

    reset(); current = &worker_thread;
    if(!setjmp(terminal)) arch_shutdown();
    CHECK(arch_wave_shutdown_error == EPERM && !destructive && sleeps == 1);

    reset(); mask = 0xf0;
    if(!setjmp(terminal)) arch_shutdown();
    CHECK(arch_wave_shutdown_error == EPERM && !destructive && cpu_sleeps == 1 && mask == 0xf0);
    hardware_periph_init_boot();
    CHECK(boot_calls == 1 && mask == 0xf0 && !wave_owner);

    reset(); check_wave_steps = true;
    hardware_shutdown();
    CHECK(!wave_owner && !initted && !platform_shutdown_owner);
    CHECK(drain_step < g2_step && g2_step < sound_step && sound_step < clear_step);

    reset(); check_wave_steps = true; g2_error = EBUSY;
    if(!setjmp(terminal)) hardware_shutdown();
    CHECK(arch_wave_shutdown_error == EBUSY && wave_owner == current && drain_step);
    CHECK(!g2_step && !sound_step && !clear_step);

    reset(); check_wave_steps = true; spu_error = EDEADLK;
    if(!setjmp(terminal)) hardware_shutdown();
    CHECK(arch_wave_shutdown_error == EDEADLK && wave_owner == current && !destructive);

    reset(); check_wave_steps = true;
    arch_auto_shutdown();
    CHECK(wave_owner == current && arch_shutdown_owns_wave_gate() && mask == 0xf0);
    CHECK(drain_step < g2_step && sound_step < clear_step && !initted);

    reset(); check_wave_steps = true;
    if(!setjmp(terminal)) exit_with_callbacks();
    CHECK(exit_steps == 3 && wave_owner == current && arch_shutdown_owns_wave_gate());
    CHECK(drain_step < g2_step && g2_step < sound_step && sound_step < clear_step);
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
    check_wave_platform();
    puts("platform: typed dispatch, persistent reason, bounded diagnostic and both terminal branches passed");
}
