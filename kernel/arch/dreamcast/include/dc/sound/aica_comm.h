/* KallistiOS ##version##

   aica_comm.h
   Copyright (C) 2000-2002 Megan Potter
   Copyright (C) 2023 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black
*/

/** \file    aica_comm.h
    \brief   Shared API for the SH4/AICA interface
    \ingroup audio_aica

    Structure and constant definitions for the SH-4/AICA interface. This file is
    included from both the ARM and SH-4 sides of the fence.

    \author Megan Potter
    \author Ruslan Rostovtsev
*/

#ifndef __DC_SOUND_AICA_COMM_H
#define __DC_SOUND_AICA_COMM_H

#ifndef __ARCH_TYPES_H
typedef unsigned long uint8;
typedef unsigned long uint32;
#endif

/** \defgroup audio_aica AICA
    \brief               API defining the SH4/AICA shared interface
    \ingroup             audio_driver
    @{
*/

/** \brief SH4-to-AICA command queue

    Command queue; one of these for passing data from the SH-4 to the
    AICA, and another for the other direction. If a command is written
    to the queue and it is longer than the amount of space between the
    head point and the queue size, the command will wrap around to
    the beginning (i.e., queue commands _can_ be split up).
*/
typedef struct aica_queue {
    uint32      head;       /**< \brief Insertion point offset (in bytes) */
    uint32      tail;       /**< \brief Removal point offset (in bytes) */
    uint32      size;       /**< \brief Queue size (in bytes) */
    uint32      valid;      /**< \brief 1 if the queue structs are valid */
    uint32      process_ok; /**< \brief 1 if it's ok to process the data */
    uint32      data;       /**< \brief Pointer to queue data buffer */
} aica_queue_t;

/** \brief Command queue struct for commanding the AICA from the SH-4 */
typedef struct aica_cmd {
    uint32      size;       /**< \brief Command data size in dwords */
    uint32      cmd;        /**< \brief Command ID */
    uint32      timestamp;  /**< \brief When to execute the command (0 == now) */
    uint32      cmd_id;     /**< \brief CmdID, for cmd/resp pairs, or chn id */
    uint32      misc[4];    /**< \brief Misc Parameters / Padding */
    uint8       cmd_data[]; /**< \brief Command data */
} aica_cmd_t;

/** \brief Maximum command size -- 256 dwords */
#define AICA_CMD_MAX_SIZE   256

/** \brief AICA command payload data for AICA_CMD_CHAN

    This is the aica_cmd_t::cmd_data for AICA_CMD_CHAN.
    Make this 16 dwords long for two aica bus queues.
*/
typedef struct aica_channel {
    uint32      cmd;        /**< \brief Command ID */
    uint32      base;       /**< \brief Sample base in RAM */
    uint32      type;       /**< \brief (8/16bit/ADPCM) */
    uint32      length;     /**< \brief Sample length */
    uint32      loop;       /**< \brief Sample looping */
    uint32      loopstart;  /**< \brief Sample loop start */
    uint32      loopend;    /**< \brief Sample loop end */
    uint32      freq;       /**< \brief Frequency */
    uint32      vol;        /**< \brief Volume 0-255 */
    uint32      pan;        /**< \brief Pan 0-255 */
    uint32      pos;        /**< \brief Sample playback pos */
    uint32      pad[5];     /**< \brief Padding */
} aica_channel_t;

/** \brief Macro for declaring an aica channel command

    Declare an aica_cmd_t big enough to hold an aica_channel_t
    using temp name T, aica_cmd_t name CMDR, and aica_channel_t name CHANR

    \param T        Buffer name
    \param CMDR     aica_cmd_t pointer name
    \param CHANR    aica_channel_t pointer name
*/
#define AICA_CMDSTR_CHANNEL(T, CMDR, CHANR) \
    uint32   T[(sizeof(aica_cmd_t) + sizeof(aica_channel_t)) / 4]; \
    aica_cmd_t  * CMDR = (aica_cmd_t *)T; \
    aica_channel_t  * CHANR = (aica_channel_t *)(CMDR->cmd_data);

/** \brief Size of an AICA channel command in words */
#define AICA_CMDSTR_CHANNEL_SIZE    ((sizeof(aica_cmd_t) + sizeof(aica_channel_t))/4)

/** \brief Payload for a synchronized 64-channel key-on command. */
typedef struct aica_channel_mask {
    uint32      low;        /**< Channels 0 through 31. */
    uint32      high;       /**< Channels 32 through 63. */
} aica_channel_mask_t;

/** \brief Macro for declaring a synchronized channel-mask command. */
#define AICA_CMDSTR_CHANNEL_MASK(T, CMDR, MASKR) \
    uint32 T[(sizeof(aica_cmd_t) + sizeof(aica_channel_mask_t)) / 4]; \
    aica_cmd_t *CMDR = (aica_cmd_t *)T; \
    aica_channel_mask_t *MASKR = (aica_channel_mask_t *)(CMDR->cmd_data)

/** \brief Size of a synchronized channel-mask command in words. */
#define AICA_CMDSTR_CHANNEL_MASK_SIZE \
    ((sizeof(aica_cmd_t) + sizeof(aica_channel_mask_t)) / 4)

/** \brief Versioned AICA firmware capability and health response. */
typedef struct aica_driver_info {
    uint32 protocol_version;       /**< Shared command protocol version. */
    uint32 firmware_version;       /**< Encoded firmware implementation version. */
    uint32 features;               /**< AICA_DRIVER_FEATURE_* bit mask. */
    uint32 uptime_ms;              /**< Firmware clock in milliseconds. */
    uint32 commands_processed;     /**< Commands with a recoverable packet boundary. */
    uint32 commands_rejected;      /**< Structurally complete commands rejected. */
    uint32 malformed_packets;      /**< Queue snapshots dropped for invalid size. */
    uint32 responses_dropped;      /**< Responses lost to insufficient queue space. */
    uint32 command_queue_size;     /**< Command queue data capacity in bytes. */
    uint32 command_queue_used;     /**< Command bytes pending at snapshot time. */
    uint32 response_queue_size;    /**< Response queue data capacity in bytes. */
    uint32 response_queue_used;    /**< Response bytes pending before this reply. */
} aica_driver_info_t;

/** \brief Macro for declaring a driver-information response packet. */
#define AICA_CMDSTR_DRIVER_INFO(T, CMDR, INFOR) \
    uint32 T[(sizeof(aica_cmd_t) + sizeof(aica_driver_info_t)) / 4]; \
    aica_cmd_t *CMDR = (aica_cmd_t *)T; \
    aica_driver_info_t *INFOR = (aica_driver_info_t *)(CMDR->cmd_data)

/** \brief Size of a driver-information response in words. */
#define AICA_CMDSTR_DRIVER_INFO_SIZE \
    ((sizeof(aica_cmd_t) + sizeof(aica_driver_info_t)) / 4)

/** \brief Shared protocol version implemented by this firmware. */
#define AICA_DRIVER_PROTOCOL_VERSION 0x00000001

/** \brief Firmware implementation version encoded as major.minor.patch. */
#define AICA_DRIVER_FIRMWARE_VERSION 0x00010000

/** \defgroup audio_aica_features Firmware Features
    \brief                               Negotiated firmware feature flags
    @{
*/
#define AICA_DRIVER_FEATURE_SYNC_CHANNELS 0x00000001
#define AICA_DRIVER_FEATURE_VALIDATION    0x00000002
#define AICA_DRIVER_FEATURE_POSITION      0x00000004
/** @} */

/** \defgroup audio_aica_cmd Commands
    \brief                   Values of commands for aica_cmd_t
    @{
*/
#define AICA_CMD_NONE       0x00000000  /**< \brief No command (dummy packet)    */
#define AICA_CMD_PING       0x00000001  /**< \brief Check for signs of life  */
#define AICA_CMD_CHAN       0x00000002  /**< \brief Perform a wavetable action   */
#define AICA_CMD_SYNC_CLOCK 0x00000003  /**< \brief Reset the millisecond clock  */
#define AICA_CMD_SYNC_CHANNELS 0x00000004 /**< \brief Key on a 64-channel mask */
#define AICA_CMD_QUERY_DRIVER 0x00000005 /**< \brief Query firmware capabilities */
/** @} */

/** \defgroup audio_aica_resp Responses
    \brief                    Values of responses to aica_cmd_t commands
    @{
 */
#define AICA_RESP_NONE      0x00000000  /**< \brief No response */
#define AICA_RESP_PONG      0x00000001  /**< \brief Response to CMD_PING */
#define AICA_RESP_DBGPRINT  0x00000002  /**< \brief Payload is a C string */
#define AICA_RESP_DRIVER_INFO 0x00000003 /**< \brief Firmware status response */
/** @} */

/** \defgroup audio_aica_ch_cmd Channel Commands
    \brief Command values (for aica_channel_t commands)
    @{
*/
#define AICA_CH_CMD_MASK    0x0000000f /**< \brief Mask for commands */

#define AICA_CH_CMD_NONE    0x00000000 /**< \brief No command */
#define AICA_CH_CMD_START   0x00000001 /**< \brief Start command */
#define AICA_CH_CMD_STOP    0x00000002 /**< \brief Stop command */
#define AICA_CH_CMD_UPDATE  0x00000003 /**< \brief Update command */
/** @} */

/** \defgroup audio_aica_ch_start Channel Start Values
    \brief                        Start values for AICA channels
    @{
*/
#define AICA_CH_START_MASK  0x00300000 /**< \brief Mask for start values */

#define AICA_CH_START_DELAY 0x00100000 /**< \brief Set params, but delay key-on */
#define AICA_CH_START_SYNC  0x00200000 /**< \brief Set key-on for all selected channels */
/** @} */

/** \defgroup audio_aica_ch_update Channel Update Values
    \brief                         Update values for AICA channels
    @{
*/
#define AICA_CH_UPDATE_MASK 0x000ff000     /**< \brief Mask for update values */

#define AICA_CH_UPDATE_SET_FREQ 0x00001000 /**< \brief frequency */
#define AICA_CH_UPDATE_SET_VOL  0x00002000 /**< \brief volume*/
#define AICA_CH_UPDATE_SET_PAN  0x00004000 /**< \brief panning */
/** @} */

/** \defgroup audio_aica_samples Sample Types
    \brief                       Types of samples used by the AICA
    @{
*/
#define AICA_SM_16BIT    0 /* Linear PCM 16-bit */
#define AICA_SM_8BIT     1 /* Linear PCM 8-bit */
#define AICA_SM_ADPCM    2 /* Yamaha ADPCM 4-bit */
#define AICA_SM_ADPCM_LS 3 /* Long stream ADPCM 4-bit */
/** @} */

/** @} */

#endif /* !__DC_SOUND_AICA_COMM_H */
