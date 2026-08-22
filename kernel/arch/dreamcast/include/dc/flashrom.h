/* KallistiOS ##version##

   dc/flashrom.h
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2008 Lawrence Sebald
   Copyright (C) 2026 Joseph Black

*/

/** \file   dc/flashrom.h
    \brief  Dreamcast flashrom read/write support.

    This file implements wrappers for the BIOS flashrom syscalls, and some
    utilities to make it easier to use the flashrom info. Note that because the
    flash writing can be such a dangerous thing potentially (I haven't deleted
    my flash to see what happens, but given the info stored here it sounds like
    a Bad Idea(tm)), extreme care should be taken if you choose to use these
    functions!

    \author Megan Potter
    \author Lawrence Sebald
*/

#ifndef __DC_FLASHROM_H
#define __DC_FLASHROM_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <kos/regfield.h>

/** \defgroup flashrom  Flashrom
    \brief              Driver for the Dreamcast's Internal Flash Storage
    \ingroup            vfs
*/

/** \defgroup fr_parts  Partitions
    \brief              Partitions available within the flashrom
    \ingroup            flashrom
    @{
*/
#define FLASHROM_PT_SYSTEM      0   /**< \brief Factory settings (read-only, 8K) */
#define FLASHROM_PT_RESERVED    1   /**< \brief reserved (all 0s, 8K) */
#define FLASHROM_PT_BLOCK_1     2   /**< \brief Block allocated (16K) */
#define FLASHROM_PT_SETTINGS    3   /**< \brief Game settings (block allocated, 32K) */
#define FLASHROM_PT_BLOCK_2     4   /**< \brief Block allocated (64K) */
/** @} */


/** \defgroup fr_blocks Logical Blocks
    \brief              Logical blocks available in the flashrom
    \ingroup            flashrom
    @{
*/
#define FLASHROM_B1_SYSCFG          0x05    /**< \brief System config (BLOCK_1) */
#define FLASHROM_B1_PW_SETTINGS_1   0x80    /**< \brief PlanetWeb settings (BLOCK_1) */
#define FLASHROM_B1_PW_SETTINGS_2   0x81    /**< \brief PlanetWeb settings (BLOCK_1) */
#define FLASHROM_B1_PW_SETTINGS_3   0x82    /**< \brief PlanetWeb settings (BLOCK_1) */
#define FLASHROM_B1_PW_SETTINGS_4   0x83    /**< \brief PlanetWeb settings (BLOCK_1) */
#define FLASHROM_B1_PW_SETTINGS_5   0x84    /**< \brief PlanetWeb settings (BLOCK_1) */
#define FLASHROM_B1_PW_PPP1         0xC0    /**< \brief PlanetWeb PPP settings (BLOCK_1) */
#define FLASHROM_B1_PW_PPP2         0xC1    /**< \brief PlanetWeb PPP settings (BLOCK_1) */
#define FLASHROM_B1_PW_DNS          0xC2    /**< \brief PlanetWeb DNS settings (BLOCK_1) */
#define FLASHROM_B1_PW_EMAIL1       0xC3    /**< \brief PlanetWeb Email settings (BLOCK_1) */
#define FLASHROM_B1_PW_EMAIL2       0xC4    /**< \brief PlanetWeb Email settings (BLOCK_1) */
#define FLASHROM_B1_PW_EMAIL_PROXY  0xC5    /**< \brief PlanetWeb Email/Proxy settings (BLOCK_1) */
#define FLASHROM_B1_DK_PPP1         0xC6    /**< \brief DreamKey PPP settings (also seen in PW) */
#define FLASHROM_B1_DK_PPP2         0xC7    /**< \brief DreamKey PPP settings (also seen in PW) */
#define FLASHROM_B1_DK_DNS          0xC8    /**< \brief DreamKey PPP settings (also seen in PW) */
#define FLASHROM_B1_IP_SETTINGS     0xE0    /**< \brief IP settings for BBA (BLOCK_1) */
#define FLASHROM_B1_EMAIL           0xE2    /**< \brief Email address (BLOCK_1) */
#define FLASHROM_B1_SMTP            0xE4    /**< \brief SMTP server setting (BLOCK_1) */
#define FLASHROM_B1_POP3            0xE5    /**< \brief POP3 server setting (BLOCK_1) */
#define FLASHROM_B1_POP3LOGIN       0xE6    /**< \brief POP3 login setting (BLOCK_1) */
#define FLASHROM_B1_POP3PASSWD      0xE7    /**< \brief POP3 password setting + proxy (BLOCK_1) */
#define FLASHROM_B1_PPPLOGIN        0xE8    /**< \brief PPP username + proxy (BLOCK_1) */
#define FLASHROM_B1_PPPPASSWD       0xE9    /**< \brief PPP passwd (BLOCK_1) */
#define FLASHROM_B1_PPPMODEM        0xEB    /**< \brief PPP modem settings */
/** @} */

#define FLASHROM_OFFSET_CRC         62      /**< \brief Location of CRC in each block */
#define FLASHROM_BLOCK_SIZE         64      /**< \brief Physical logical-block record size */
#define FLASHROM_BLOCK_DATA_SIZE    60      /**< \brief Application data bytes in a record */

/** \defgroup fr_errs   Error Values
    \brief              Error values for the flashrom_get_block() function
    \ingroup            flashrom
    @{
*/
#define FLASHROM_ERR_NONE           0       /**< \brief Success */
#define FLASHROM_ERR_NOT_FOUND      -1      /**< \brief Block not found */
#define FLASHROM_ERR_NO_PARTITION   -2      /**< \brief Partition not found */
#define FLASHROM_ERR_READ_PART      -3      /**< \brief Error reading partition */
#define FLASHROM_ERR_BAD_MAGIC      -4      /**< \brief Invalid block magic */
#define FLASHROM_ERR_BOGUS_PART     -5      /**< \brief Bogus partition size */
#define FLASHROM_ERR_NOMEM          -6      /**< \brief Memory allocation failure */
#define FLASHROM_ERR_READ_BITMAP    -7      /**< \brief Error reading bitmap */
#define FLASHROM_ERR_EMPTY_PART     -8      /**< \brief Empty partition */
#define FLASHROM_ERR_READ_BLOCK     -9      /**< \brief Error reading block */
#define FLASHROM_ERR_BAD_DATA       -10     /**< \brief Record contents are inconsistent */
#define FLASHROM_ERR_NO_SPACE       -11     /**< \brief Append area is full */
#define FLASHROM_ERR_WRITE_BITMAP   -12     /**< \brief Allocation-map write failed */
#define FLASHROM_ERR_WRITE_BLOCK    -13     /**< \brief Record write failed */
#define FLASHROM_ERR_VERIFY         -14     /**< \brief Programmed data did not verify */
/** @} */

/** \brief   Retrieve information about the given partition.
    \ingroup flashrom

    This function implements the FLASHROM_INFO syscall; given a partition ID,
    return two ints specifying the beginning and the size of the partition
    (respectively) inside the flashrom.

    \param  part            The partition ID in question.
    \param  start_out       Buffer for storing the start address.
    \param  size_out        Buffer for storing the size of the partition.
    \retval 0               On success.
    \retval -1              On error.
*/
int flashrom_info(int part, int *start_out, int *size_out);

/** \brief   Read data from the flashrom.
    \ingroup flashrom

    This function implements the FLASHROM_READ syscall; given a flashrom offset,
    an output buffer, and a count, this reads data from the flashrom.

    \param  offset          The offset to read from.
    \param  buffer_out      Space to read into.
    \param  bytes           The number of bytes to read.
    \return                 The number of bytes read if successful, or -1
                            otherwise.
*/
int flashrom_read(int offset, void *buffer_out, int bytes);

/** \brief   Write data to the flashrom.
    \ingroup flashrom

    This function implements the FLASHROM_WRITE syscall; given a flashrom
    offset, an input buffer, and a count, this writes data to the flashrom.

    \note It is not possible to write ones to the flashrom over zeros. If you
    want to do this, you must save the old data in the flashrom, delete it out,
    and save the new data back.

    \param  offset          The offset to write at.
    \param  buffer          The data to write.
    \param  bytes           The number of bytes to write.
    \return                 The number of bytes written if successful, -1
                            otherwise.
*/
int flashrom_write(int offset, void *buffer, int bytes);

/** \brief   Delete data from the flashrom.
    \ingroup flashrom

    This function implements the FLASHROM_DELETE syscall; given a partition
    offset, that entire partition of the flashrom will be deleted and all data
    will be reset to 0xFF bytes.

    \note This does not rewrite the magic block to the start of the partition.
    It is your responsibility to do this after running this function.

    \param  offset          The offset of the start of the partition to erase.
    \retval 0               On success.
    \retval -1              On error.
*/
int flashrom_delete(int offset);


/* Medium-level functions */
/** \brief   Get a logical block from the specified partition.
    \ingroup flashrom

    This function retrieves the specified block ID from the given partition. The
    newest version of the data is returned.

    \param  partid          The partition ID to look in.
    \param  blockid         The logical block ID to look for.
    \param  buffer_out      Space to store the complete 64-byte record.
    \return                 0 on success, <0 on error.
    \see    fr_errs
*/
int flashrom_get_block(int partid, int blockid, uint8_t *buffer_out);

/** \brief   Information about a resolved logical block.
    \ingroup flashrom

    \headerfile dc/flashrom.h
*/
typedef struct flashrom_block_info {
    uint16_t logical_id;       /**< \brief Logical block identifier. */
    uint16_t physical_block;   /**< \brief Physical record within the partition. */
} flashrom_block_info_t;

/** \brief   Read the payload of the newest valid logical block.
    \ingroup flashrom

    Unlike flashrom_get_block(), this function copies only the 60-byte payload;
    it does not expose the two-byte logical identifier or record CRC. The block
    map, physical bounds, record identifier, and record CRC are validated before
    the payload is published.

    \param  partid          Partition ID to search.
    \param  blockid         Logical block ID to locate.
    \param  data_out        Optional 60-byte payload destination.
    \param  info_out        Optional resolved-block information destination.
    \return                 0 on success, or a negative value from fr_errs.
*/
int flashrom_read_block(int partid, uint16_t blockid, void *data_out,
                        flashrom_block_info_t *info_out);

/** \brief   Append and verify a logical block payload.
    \ingroup flashrom

    This function programs a new 64-byte record without erasing or refreshing
    the partition. The allocation map is committed before the record and the
    record CRC is programmed last. An interrupted operation may consume one
    physical record, but the previous valid value remains readable.

    This API never erases a full partition. FLASHROM_ERR_NO_SPACE tells the
    caller that explicit maintenance would be required. It must be called from
    thread context.

    \note A write or verification error has ambiguous completion semantics.
          Reread the logical block before retrying because the final program
          operation may have completed before reporting an error.

    \param  partid          Block-allocated partition ID (2 through 4).
    \param  blockid         Logical block identifier.
    \param  data            Exact 60-byte payload to append.
    \param  info_out        Optional committed-record information.
    \return                 0 on success, or a negative value from fr_errs.
*/
int flashrom_append_block(int partid, uint16_t blockid, const void *data,
                          flashrom_block_info_t *info_out);


/* Higher level functions */

/** \defgroup fr_langs  Language Settings
    \brief              Language settings possible in the BIOS menu
    \ingroup            flashrom

    This set of constants will be returned as the language value in the
    flashrom_syscfg_t structure.

    @{
*/
#define FLASHROM_LANG_JAPANESE  0   /**< \brief Japanese language code */
#define FLASHROM_LANG_ENGLISH   1   /**< \brief English language code */
#define FLASHROM_LANG_GERMAN    2   /**< \brief German language code */
#define FLASHROM_LANG_FRENCH    3   /**< \brief French language code */
#define FLASHROM_LANG_SPANISH   4   /**< \brief Spanish language code */
#define FLASHROM_LANG_ITALIAN   5   /**< \brief Italian language code */
/** @} */

/** \brief   System configuration structure.
    \ingroup flashrom

    This structure is filled in with the settings set in the BIOS from the
    flashrom_get_syscfg() function.

    \headerfile dc/flashrom.h
*/
typedef struct flashrom_syscfg {
    int language;   /**< \brief Language setting.
                         \see fr_langs */
    int audio;      /**< \brief Stereo/mono setting. 0 == mono, 1 == stereo */
    int autostart;  /**< \brief Autostart discs? 0 == off, 1 == on */
} flashrom_syscfg_t;

/** \brief   Extended validated system configuration.
    \ingroup flashrom

    \headerfile dc/flashrom.h
*/
typedef struct flashrom_syscfg_ex {
    uint32_t settings_time; /**< \brief Seconds since 1950-01-01 when last set. */
    int language;           /**< \brief Language setting from fr_langs. */
    int audio;              /**< \brief 0 for mono, 1 for stereo. */
    int autostart;          /**< \brief 0 for disabled, 1 for enabled. */
} flashrom_syscfg_ex_t;

/** \brief   Retrieve the current system configuration settings.
    \ingroup flashrom

    \param  out             Storage for the configuration.
    \return                 0 on success, <0 on error.
    \see    fr_errs
*/
int flashrom_get_syscfg(flashrom_syscfg_t *out);

/** \brief   Retrieve and validate the complete known system configuration.
    \ingroup flashrom

    This strict form rejects unknown language, audio, or autostart encodings
    instead of silently converting them to plausible values.

    \param  out             Storage for the extended configuration.
    \return                 0 on success, or a negative value from fr_errs.
*/
int flashrom_get_syscfg_ex(flashrom_syscfg_ex_t *out);

/** \brief   Append a verified system-configuration update.
    \ingroup flashrom

    Unknown bytes from the current configuration record are preserved. The
    supplied time is stored as seconds since 1950-01-01. No partition erase or
    automatic refresh is performed. This function must be called from thread
    context.

    \note On a write or verification error, reread the configuration before
          retrying because the new record may already be valid.

    \param  settings        Validated configuration to store.
    \return                 0 on success, or a negative value from fr_errs.
*/
int flashrom_set_syscfg_ex(const flashrom_syscfg_ex_t *settings);

/** \brief   Decode a system-configuration block payload.
    \ingroup flashrom

    This hardware-independent helper is useful for validating flash images and
    test fixtures. The input is the 60-byte payload returned by
    flashrom_read_block().

    \param  data            System-configuration payload.
    \param  out             Storage for the decoded configuration.
    \return                 0 on success, or FLASHROM_ERR_BAD_DATA.
*/
int flashrom_syscfg_decode(const uint8_t data[FLASHROM_BLOCK_DATA_SIZE],
                           flashrom_syscfg_ex_t *out);

/** \defgroup fr_play_history Play History
    \brief              Validated access to title play-history records
    \ingroup            flashrom
    @{
*/
#define FLASHROM_PLAY_HISTORY_SLOTS       100 /**< \brief Maximum title slots. */
#define FLASHROM_PLAY_HISTORY_USER_BYTES  32  /**< \brief Per-title user bytes. */
#define FLASHROM_PLAY_HISTORY_BUCKETS     24  /**< \brief Play-time buckets. */

/** \brief   Decoded play history for one title.

    All integer members are converted to SH-4 host order. Time fields use the
    flash record's seconds-since-1950 representation; play-time buckets and
    network_total_minutes are measured in minutes.

    \headerfile dc/flashrom.h
*/
typedef struct flashrom_play_history {
    uint8_t version;                 /**< \brief Record format version. */
    uint8_t autosave;                /**< \brief Title autosave setting. */
    char product_number[11];         /**< \brief Ten-byte product number plus NUL. */
    char product_name[49];           /**< \brief Primary title plus NUL. */
    char product_name_alt[45];       /**< \brief Alternate title plus NUL. */
    uint32_t kind;                   /**< \brief Title category value. */
    uint32_t first_start_time;       /**< \brief First start time. */
    uint8_t peripheral_info[6];      /**< \brief Recorded peripheral summary. */
    uint32_t previous_start_time;    /**< \brief Previous start time. */
    uint16_t start_count;            /**< \brief Number of starts. */
    uint16_t play_time[FLASHROM_PLAY_HISTORY_BUCKETS];
    uint16_t load_count;             /**< \brief Number of load events. */
    uint32_t reserved_packet2;       /**< \brief Preserved packet-2 bytes. */
    uint16_t save_count;             /**< \brief Number of save events. */
    uint8_t evaluation;              /**< \brief Title-defined play evaluation. */
    uint8_t progress;                /**< \brief Title-defined progress value. */
    uint32_t first_network_time;     /**< \brief First network-play time. */
    uint32_t previous_network_time;  /**< \brief Previous network-play time. */
    uint16_t network_count;          /**< \brief Network-play count. */
    uint16_t network_total_minutes;  /**< \brief Total network-play minutes. */
    uint8_t user_data[FLASHROM_PLAY_HISTORY_USER_BYTES];
    uint8_t reserved_packet3[10];    /**< \brief Preserved packet-3 bytes. */
    uint16_t save_occurrences;       /**< \brief Flash save occurrence count. */
} flashrom_play_history_t;

/** \brief   Encode four play-history block payloads.

    This hardware-independent helper converts host-order fields to their flash
    representation and calculates the title CRC. Fixed-width strings and
    reserved bytes are copied exactly from the structure.

    \param  history         Host-order play-history record.
    \param  packets         Four encoded 60-byte payloads.
    \return                 0 on success, or FLASHROM_ERR_BAD_DATA.
*/
int flashrom_play_history_encode(
    const flashrom_play_history_t *history,
    uint8_t packets[4][FLASHROM_BLOCK_DATA_SIZE]);

/** \brief   Decode four play-history block payloads.

    This hardware-independent helper validates the title CRC and converts all
    multi-byte fields to host order.

    \param  packets         Four contiguous 60-byte payloads.
    \param  out             Storage for the decoded record.
    \return                 0 on success, or FLASHROM_ERR_BAD_DATA.
*/
int flashrom_play_history_decode(
    const uint8_t packets[4][FLASHROM_BLOCK_DATA_SIZE],
    flashrom_play_history_t *out);

/** \brief   Read and validate a play-history slot.

    \param  slot            Slot number in the range 0 through 99.
    \param  out             Storage for the decoded record.
    \return                 0 on success, or a negative value from fr_errs.
*/
int flashrom_play_history_read(unsigned int slot,
                               flashrom_play_history_t *out);

/** \brief   Find play history by its ten-byte product number.

    Product numbers are fixed-width identifiers and need not contain a NUL.

    \param  product_number  Ten-byte product number to locate.
    \param  out             Optional storage for the decoded record.
    \param  slot_out        Optional storage for the matching slot.
    \return                 0 on success, or a negative value from fr_errs.
*/
int flashrom_play_history_find(const char product_number[10],
                               flashrom_play_history_t *out,
                               unsigned int *slot_out);

/** \brief   Result of a multi-packet play-history update.

    requested_mask identifies packets whose encoded bytes differed from the
    latest stored copy. committed_mask identifies packets whose append and
    readback verification completed. If failed_packet is nonnegative, that
    packet has ambiguous completion semantics and must be reread.

    \headerfile dc/flashrom.h
*/
typedef struct flashrom_play_history_write_result {
    uint8_t requested_mask;          /**< \brief Packets selected for append. */
    uint8_t committed_mask;          /**< \brief Packets verified this call. */
    int failed_packet;               /**< \brief Ambiguous packet, or -1. */
    flashrom_block_info_t records[4];/**< \brief Verified physical records. */
} flashrom_play_history_write_result_t;

/** \brief   Append changed packets for one play-history slot.

    The function compares the encoded record with the latest stored packets,
    preflights enough erased records for every change, and appends only packets
    whose bytes differ. Packets 2 and 3 are written first, followed by packet 1
    and packet 0, so a changed title identity is not accepted until its
    cross-packet CRC agrees.

    This operation cannot make all four records power-loss atomic because the
    on-flash format provides no transaction marker. An interrupted counter-only
    update may expose the successfully committed counters. An interruption
    between changed packets 1 and 0 can make the title temporarily unreadable
    until packet 0 is retried. No partition erase or refresh is performed.

    \param  slot            Slot number in the range 0 through 99.
    \param  history         Complete desired host-order record.
    \param  result_out      Optional detailed packet result.
    \return                 0 on success, or a negative value from fr_errs.
*/
int flashrom_play_history_write(
    unsigned int slot, const flashrom_play_history_t *history,
    flashrom_play_history_write_result_t *result_out);
/** @} */


/** \defgroup fr_region Region Settings
    \brief              Region settings possible in the system
    \ingroup            flashrom

    One of these values should be returned by flashrom_get_region().

    @{
*/
#define FLASHROM_REGION_UNKNOWN 0   /**< \brief Unknown region */
#define FLASHROM_REGION_JAPAN   1   /**< \brief Japanese region */
#define FLASHROM_REGION_US      2   /**< \brief US/Canada region */
#define FLASHROM_REGION_EUROPE  3   /**< \brief European region */
/** @} */

/** \brief   Retrieve the console's region code.
    \ingroup flashrom

    This function attempts to find the region of the Dreamcast. It may or may
    not work on 100% of Dreamcasts, apparently.

    \return                 A region code (>=0), or error (<0).
    \see    fr_region
    \see    fr_errs
*/
int flashrom_get_region(void);

/** \defgroup fr_method Connection Methods
    \brief              Connection method types stored within flashrom
    \ingroup            flashrom

    These values are representative of what type of ISP is configured in the
    flashrom.

    @{
*/
#define FLASHROM_ISP_DIALUP 0   /**< \brief Dialup ISP */
#define FLASHROM_ISP_DHCP   1   /**< \brief DHCP-based ethernet */
#define FLASHROM_ISP_PPPOE  2   /**< \brief PPPoE-based ethernet */
#define FLASHROM_ISP_STATIC 3   /**< \brief Static IP-based ethernet */
/** @} */

/** \defgroup fr_fields ISP Config Fields
    \brief              Valid field constants for the flashrom_ispcfg_t struct
    \ingroup            flashrom

    The valid_fields field of the flashrom_ispcfg_t will have some combination
    of these ORed together to represent what data is filled in and believed
    valid.

    @{
*/
#define FLASHROM_ISP_IP         BIT(0)   /**< \brief Static IP address */
#define FLASHROM_ISP_NETMASK    BIT(1)   /**< \brief Netmask */
#define FLASHROM_ISP_BROADCAST  BIT(2)   /**< \brief Broadcast address */
#define FLASHROM_ISP_GATEWAY    BIT(3)   /**< \brief Gateway address */
#define FLASHROM_ISP_DNS        BIT(4)   /**< \brief DNS servers */
#define FLASHROM_ISP_HOSTNAME   BIT(5)   /**< \brief Hostname */
#define FLASHROM_ISP_EMAIL      BIT(6)   /**< \brief Email address */
#define FLASHROM_ISP_SMTP       BIT(7)   /**< \brief SMTP server */
#define FLASHROM_ISP_POP3       BIT(8)   /**< \brief POP3 server */
#define FLASHROM_ISP_POP3_USER  BIT(9)   /**< \brief POP3 username */
#define FLASHROM_ISP_POP3_PASS  (1 << 10)   /**< \brief POP3 password */
#define FLASHROM_ISP_PROXY_HOST (1 << 11)   /**< \brief Proxy hostname */
#define FLASHROM_ISP_PROXY_PORT (1 << 12)   /**< \brief Proxy port */
#define FLASHROM_ISP_PPP_USER   (1 << 13)   /**< \brief PPP username */
#define FLASHROM_ISP_PPP_PASS   (1 << 14)   /**< \brief PPP password */
#define FLASHROM_ISP_OUT_PREFIX (1 << 15)   /**< \brief Outside dial prefix */
#define FLASHROM_ISP_CW_PREFIX  (1 << 16)   /**< \brief Call waiting prefix */
#define FLASHROM_ISP_REAL_NAME  (1 << 17)   /**< \brief Real name */
#define FLASHROM_ISP_MODEM_INIT (1 << 18)   /**< \brief Modem init string */
#define FLASHROM_ISP_AREA_CODE  (1 << 19)   /**< \brief Area code */
#define FLASHROM_ISP_LD_PREFIX  (1 << 20)   /**< \brief Long distance prefix */
#define FLASHROM_ISP_PHONE1     (1 << 21)   /**< \brief Phone number 1 */
#define FLASHROM_ISP_PHONE2     (1 << 22)   /**< \brief Phone number 2 */
/** @} */

/** \defgroup fr_flags  ISP Config Flags
    \brief              Flags for the flashrom_ispcfg_t struct
    \ingroup            flashrom

    The flags field of the flashrom_ispcfg_t will have some combination of these
    ORed together to represent what settings were set.

    @{
*/
#define FLASHROM_ISP_DIAL_AREACODE  BIT(0)   /**< \brief Dial area code before number */
#define FLASHROM_ISP_USE_PROXY      BIT(1)   /**< \brief Proxy enabled */
#define FLASHROM_ISP_PULSE_DIAL     BIT(2)   /**< \brief Pulse dialing (instead of tone) */
#define FLASHROM_ISP_BLIND_DIAL     BIT(3)   /**< \brief Blind dial (don't wait for tone) */
/** @} */

/** \brief   ISP configuration structure.
    \ingroup flashrom

    This structure will be filled in by flashrom_get_ispcfg() (DreamPassport) or
    flashrom_get_pw_ispcfg() (PlanetWeb). Thanks to Sam Steele for the
    information about DreamPassport's ISP settings.

    \headerfile dc/flashrom.h
*/
typedef struct flashrom_ispcfg {
    int       method;         /**< \brief DHCP, Static, dialup(?), PPPoE
                                 \see   fr_method */
    uint32_t  valid_fields;   /**< \brief Which fields are valid?
                                 \see   fr_fields */
    uint32_t  flags;          /**< \brief Various flags that can be set in options
                                 \see   fr_flags */

    uint8_t ip[4];          /**< \brief Host IP address */
    uint8_t nm[4];          /**< \brief Netmask */
    uint8_t bc[4];          /**< \brief Broadcast address */
    uint8_t gw[4];          /**< \brief Gateway address */
    uint8_t dns[2][4];      /**< \brief DNS servers (2) */
    int     proxy_port;     /**< \brief Proxy server port */
    char    hostname[24];   /**< \brief DHCP/Host name */
    char    email[64];      /**< \brief Email address */
    char    smtp[31];       /**< \brief SMTP server */
    char    pop3[31];       /**< \brief POP3 server */
    char    pop3_login[20]; /**< \brief POP3 login */
    char    pop3_passwd[32];/**< \brief POP3 passwd */
    char    proxy_host[31]; /**< \brief Proxy server hostname */
    char    ppp_login[29];  /**< \brief PPP login */
    char    ppp_passwd[20]; /**< \brief PPP password */
    char    out_prefix[9];  /**< \brief Outside dial prefix */
    char    cw_prefix[9];   /**< \brief Call waiting prefix */
    char    real_name[31];  /**< \brief The "Real Name" field of PlanetWeb */
    char    modem_init[33]; /**< \brief The modem init string to use */
    char    area_code[4];   /**< \brief The area code the user is in */
    char    ld_prefix[21];  /**< \brief The long-distance dial prefix */
    char    p1_areacode[4]; /**< \brief Phone number 1's area code */
    char    phone1[26];     /**< \brief Phone number 1 */
    char    p2_areacode[4]; /**< \brief Phone number 2's area code */
    char    phone2[26];     /**< \brief Phone number 2 */
} flashrom_ispcfg_t;

/** \brief   Retrieve DreamPassport's ISP configuration.
    \ingroup flashrom

    This function retrieves the console's ISP settings as set by DreamPassport,
    if they exist. You should check the valid_fields bitfield for the part of
    the struct you want before relying on the data.

    \param  out             Storage for the structure.
    \retval 0               On success.
    \retval -1              On error (no settings found, or other errors).
*/
int flashrom_get_ispcfg(flashrom_ispcfg_t *out);

/** \brief   Retrieve PlanetWeb's ISP configuration.
    \ingroup flashrom

    This function retrieves the console's ISP settings as set by PlanetWeb (1.0
    and 2.1 have been verified to work), if they exist. You should check the
    valid_fields bitfield for the part of the struct you want before relying on
    the data.

    \param  out             Storage for the structure.
    \retval 0               On success.
    \retval -1              On error (i.e, no PlanetWeb settings found).
*/
int flashrom_get_pw_ispcfg(flashrom_ispcfg_t *out);

/* More to come later */

__END_DECLS

#endif  /* __DC_FLASHROM_H */
