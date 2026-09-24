#ifndef GAPS_TEST_FS_DCLOAD_H
#define GAPS_TEST_FS_DCLOAD_H
#define DCLOAD_TYPE_NONE (-1)
#define DCLOAD_TYPE_SER 0
#define DCLOAD_TYPE_IP 1
extern int dcload_type;
int syscall_dcload_detected(void);
#endif
