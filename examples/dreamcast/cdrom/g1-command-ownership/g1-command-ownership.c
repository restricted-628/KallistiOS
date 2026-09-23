/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/cdrom.h>
#include <dc/syscalls.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

enum { COMPLETE, ABORT, RESET, LOCK_FAIL };
static bool testing, held;
static unsigned int scenario, polls, locks, unlocks, aborts, resets, inits, failures;

#define CHECK(c) do { if(!(c)) { ++failures; \
    printf("G1-COMMAND: failed line=%d case=%u\n", __LINE__, scenario); } } while(0)

int __real_g1_bus_lock(void);
int __real_g1_bus_unlock(void);
int __real_thd_poll(thd_cb_t cb, void *data, unsigned long timeout);

int __wrap_g1_bus_lock(void) {
    if(!testing)
        return __real_g1_bus_lock();
    ++locks;
    CHECK(!held);
    if(scenario == LOCK_FAIL) {
        errno = EIO;
        return -1;
    }
    held = true;
    return 0;
}

int __wrap_g1_bus_unlock(void) {
    if(!testing)
        return __real_g1_bus_unlock();
    CHECK(held);
    if(scenario == ABORT || scenario == RESET)
        CHECK(aborts == 1);
    if(scenario == RESET)
        CHECK(resets == 1 && inits == 1);
    ++unlocks;
    held = false;
    return 0;
}

int __wrap_thd_poll(thd_cb_t cb, void *data, unsigned long timeout) {
    if(!testing)
        return __real_thd_poll(cb, data, timeout);
    CHECK(held);
    ++polls;
    if(polls == 2 && scenario != COMPLETE)
        return 0;
    if(polls == 3 && scenario == RESET)
        return 0;
    return cb(data);
}

gdc_cmd_hnd_t __wrap_syscall_gdrom_send_command(cd_cmd_code_t cmd, void *params) {
    CHECK(testing && held && cmd == CD_CMD_REQ_STAT && !params);
    return 77;
}

void __wrap_syscall_gdrom_exec_server(void) {
    CHECK(testing && held);
}

cd_cmd_chk_t __wrap_syscall_gdrom_check_command(gdc_cmd_hnd_t hnd,
                                              cd_cmd_chk_status_t *status) {
    CHECK(testing && held && hnd == 77);
    memset(status, 0, sizeof(*status));
    return CD_CMD_COMPLETED;
}

int __wrap_syscall_gdrom_abort_command(gdc_cmd_hnd_t hnd) {
    CHECK(testing && held && hnd == 77 && locks == 1 && unlocks == 0);
    ++aborts;
    return 0;
}

void __wrap_syscall_gdrom_reset(void) {
    CHECK(testing && held && unlocks == 0);
    ++resets;
}

void __wrap_syscall_gdrom_init(void) {
    CHECK(testing && held && unlocks == 0);
    ++inits;
}

int main(void) {
    testing = true;
    for(scenario = COMPLETE; scenario <= LOCK_FAIL; ++scenario) {
        int result;
        polls = locks = unlocks = aborts = resets = inits = 0;
        result = cdrom_exec_cmd_timed(CD_CMD_REQ_STAT, NULL, 20);
        CHECK(result == (scenario == COMPLETE ? ERR_OK :
                         scenario == LOCK_FAIL ? ERR_SYS : ERR_TIMEOUT));
        CHECK(locks == 1 && !held);
        CHECK(unlocks == (scenario == LOCK_FAIL ? 0u : 1u));
        CHECK(aborts == (scenario == ABORT || scenario == RESET ? 1u : 0u));
        CHECK(resets == (scenario == RESET ? 1u : 0u) && inits == resets);
        CHECK(cdrom_abort_cmd(20, false) == ERR_NO_ACTIVE);
        CHECK(locks == 1);
    }
    testing = false;
    printf("G1-COMMAND: %s cases=4\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
