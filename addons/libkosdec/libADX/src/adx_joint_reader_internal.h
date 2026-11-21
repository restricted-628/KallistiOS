#ifndef KOSDEC_ADX_JOINT_READER_INTERNAL_H
#define KOSDEC_ADX_JOINT_READER_INTERNAL_H
#include <kosdec/adx_joint_reader.h>
bool media_adx_joint_reader_enable_live_sfa(struct kosdec_adx_joint_reader *);
bool media_adx_joint_reader_set_sfa_length(struct kosdec_adx_joint_reader *,
    struct kosdec_adx_joint *, uint32_t frames);
struct kosdec_adx_joint_read_result media_adx_joint_reader_read_pipe(
    struct kosdec_adx_joint_reader *, size_t room, size_t tail);
enum kosdec_adx_status media_adx_joint_sector_step(struct kosdec_adx_joint_reader *);
enum kosdec_adx_joint_status media_adx_joint_sector_close(struct kosdec_adx_joint_reader *);
#endif
