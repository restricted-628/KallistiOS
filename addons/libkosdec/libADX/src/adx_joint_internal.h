#ifndef MEDIA_ADX_JOINT_INTERNAL_H
#define MEDIA_ADX_JOINT_INTERNAL_H
#include <kosdec/adx_joint.h>
/* Serialized, no active loans. Keeps the final-input flag and token sequence. */
bool media_adx_joint_rewind_memory(struct kosdec_adx_joint *, uint32_t offset);
#endif
