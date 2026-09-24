#ifndef FLASHROM_LAYOUT_TEST_DBGLOG_H
#define FLASHROM_LAYOUT_TEST_DBGLOG_H

#define DBG_ERROR 1
#define DBG_WARNING 2

int dbglog(int level, const char *format, ...);

#endif
