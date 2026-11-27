/* Private KOS VM source check for the first direct-ISO target. */
#ifndef KOS_VM_DEMAND_SOURCE_PRIVATE_H
#define KOS_VM_DEMAND_SOURCE_PRIVATE_H
#include <kos/fs.h>
int fs_iso9660_validate_demand_source(file_t fd);
#endif
