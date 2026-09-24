/* KallistiOS ##version##

   flashrom-read-only.c
   Copyright (C) 2026 Joseph Black
*/

#include <stdio.h>

#include <dc/flashrom.h>

int main(int argc, char **argv) {
    flashrom_syscfg_ex_t cfg;
    flashrom_play_history_t history;
    int cfg_result, history_result;

    (void)argc;
    (void)argv;

    cfg_result = flashrom_get_syscfg_ex(&cfg);
    if(cfg_result == FLASHROM_ERR_NONE) {
        printf("System settings: time=%lu language=%d audio=%s autostart=%s\n",
               (unsigned long)cfg.settings_time, cfg.language,
               cfg.audio ? "stereo" : "mono",
               cfg.autostart ? "on" : "off");
    }
    else {
        printf("System settings unavailable: %d\n", cfg_result);
    }

    history_result = flashrom_play_history_read(0, &history);
    if(history_result == FLASHROM_ERR_NONE) {
        printf("History slot 0: %.10s / %s\n", history.product_number,
               history.product_name);
        printf("starts=%u loads=%u saves=%u network=%u\n",
               history.start_count, history.load_count, history.save_count,
               history.network_count);
    }
    else {
        printf("History slot 0 unavailable: %d\n", history_result);
    }

    printf("KOSFLASH cfg=%d history=%d\n", cfg_result, history_result);
    return 0;
}
