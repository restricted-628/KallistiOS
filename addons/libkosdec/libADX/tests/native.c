/* Run the actual SH-4 archive with the existing synthetic numerical oracles. */
#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

int adx_setup_run(void);
int adx_expanded_run(void);
int adx_clipped_history_run(void);
int adx_loop_metadata_run(void);
int adx_profiles_run(void);
int adx_sfa_run(void);
int adx_joint_run(void);

int main(void) {
    int status = adx_setup_run();
    status |= adx_expanded_run();
    status |= adx_clipped_history_run();
    status |= adx_loop_metadata_run();
    status |= adx_profiles_run();
    status |= adx_sfa_run();
    status |= adx_joint_run();
    printf("ADX-NATIVE %s\n",status ? "FAIL" : "PASS");
    fflush(stdout);
    for(;;) thd_sleep(1000);
}
