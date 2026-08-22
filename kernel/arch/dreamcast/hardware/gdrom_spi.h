/* KallistiOS ##version##

   hardware/gdrom_spi.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __KOS_INTERNAL_GDROM_SPI_H
#define __KOS_INTERNAL_GDROM_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDROM_SPI_PACKET_SIZE 12u
#define GDROM_SPI_MAX_U24     0x00ffffffu
#define GDROM_SPI_STATUS_SIZE 10u
#define GDROM_SPI_ERROR_SIZE  10u
#define GDROM_SPI_TOC_SIZE    408u
#define GDROM_SPI_SESSION_SIZE 6u
#define GDROM_SPI_MODE_SIZE    32u
#define GDROM_SPI_MODE_WRITABLE_START 2u
#define GDROM_SPI_MODE_WRITABLE_SIZE  8u

typedef enum gdrom_spi_opcode {
    GDROM_SPI_TEST_UNIT = 0x00,
    GDROM_SPI_REQ_STAT  = 0x10,
    GDROM_SPI_REQ_MODE  = 0x11,
    GDROM_SPI_SET_MODE  = 0x12,
    GDROM_SPI_REQ_ERROR = 0x13,
    GDROM_SPI_GET_TOC   = 0x14,
    GDROM_SPI_REQ_SES   = 0x15,
    GDROM_SPI_CD_OPEN   = 0x16,
    GDROM_SPI_CD_PLAY   = 0x20,
    GDROM_SPI_CD_SEEK   = 0x21,
    GDROM_SPI_CD_SCAN   = 0x22,
    GDROM_SPI_CD_READ   = 0x30,
    GDROM_SPI_CD_READ2  = 0x31,
    GDROM_SPI_GET_SCD   = 0x40
} gdrom_spi_opcode_t;

typedef enum gdrom_spi_point_type {
    GDROM_SPI_POINT_FAD = 1,
    GDROM_SPI_POINT_MSF = 2
} gdrom_spi_point_type_t;

typedef enum gdrom_spi_seek_type {
    GDROM_SPI_SEEK_FAD   = 1,
    GDROM_SPI_SEEK_MSF   = 2,
    GDROM_SPI_SEEK_STOP  = 3,
    GDROM_SPI_SEEK_PAUSE = 4
} gdrom_spi_seek_type_t;

typedef enum gdrom_spi_expected_type {
    GDROM_SPI_EXPECT_ANY          = 0,
    GDROM_SPI_EXPECT_CDDA         = 1,
    GDROM_SPI_EXPECT_MODE1        = 2,
    GDROM_SPI_EXPECT_MODE2        = 3,
    GDROM_SPI_EXPECT_MODE2_FORM1  = 4,
    GDROM_SPI_EXPECT_MODE2_FORM2  = 5,
    GDROM_SPI_EXPECT_MODE2_NON_XA = 6
} gdrom_spi_expected_type_t;

typedef enum gdrom_spi_data_select {
    GDROM_SPI_SELECT_OTHER     = 0x1,
    GDROM_SPI_SELECT_DATA      = 0x2,
    GDROM_SPI_SELECT_SUBHEADER = 0x4,
    GDROM_SPI_SELECT_HEADER    = 0x8
} gdrom_spi_data_select_t;

typedef enum gdrom_spi_subcode_format {
    GDROM_SPI_SUBCODE_RAW  = 0,
    GDROM_SPI_SUBCODE_Q    = 1,
    GDROM_SPI_SUBCODE_UPC  = 2,
    GDROM_SPI_SUBCODE_ISRC = 3
} gdrom_spi_subcode_format_t;

typedef struct gdrom_spi_packet {
    uint8_t bytes[GDROM_SPI_PACKET_SIZE];
} gdrom_spi_packet_t;

typedef struct gdrom_spi_status {
    uint8_t status;
    uint8_t disc_format;
    uint8_t repeat_count;
    uint8_t control;
    uint8_t adr;
    uint8_t track;
    uint8_t index;
    uint32_t fad;
    uint8_t max_read_retries;
} gdrom_spi_status_t;

typedef struct gdrom_spi_error {
    uint8_t sense_key;
    uint32_t command_specific_information;
    uint8_t asc;
    uint8_t ascq;
} gdrom_spi_error_t;

typedef struct gdrom_spi_toc {
    uint32_t entry[99];
    uint32_t first;
    uint32_t last;
    uint32_t leadout;
} gdrom_spi_toc_t;

typedef struct gdrom_spi_session {
    uint8_t status;
    uint8_t number_or_track;
    uint32_t fad;
} gdrom_spi_session_t;

typedef struct gdrom_spi_mode {
    uint8_t speed;
    uint16_t standby_seconds;
    bool read_continuous;
    bool ecc_retry;
    bool read_retry;
    bool form2_retry;
    uint8_t read_retry_count;
    uint8_t drive_information[8];
    uint8_t system_version[8];
    uint8_t system_date[6];
} gdrom_spi_mode_t;

_Static_assert(sizeof(gdrom_spi_packet_t) == GDROM_SPI_PACKET_SIZE,
               "GD-ROM SPI packets must be exactly 12 bytes");

void gdrom_spi_test_unit(gdrom_spi_packet_t *packet);
void gdrom_spi_open(gdrom_spi_packet_t *packet);

int gdrom_spi_req_stat(gdrom_spi_packet_t *packet, uint8_t start,
                       uint8_t allocation);
int gdrom_spi_req_mode(gdrom_spi_packet_t *packet, uint8_t start,
                       uint8_t allocation);
int gdrom_spi_set_mode(gdrom_spi_packet_t *packet, uint8_t start,
                       uint8_t allocation);
void gdrom_spi_req_error(gdrom_spi_packet_t *packet, uint8_t allocation);
void gdrom_spi_get_toc(gdrom_spi_packet_t *packet, bool high_density,
                       uint16_t allocation);
int gdrom_spi_req_session(gdrom_spi_packet_t *packet, uint8_t session,
                          uint8_t allocation);

int gdrom_spi_play(gdrom_spi_packet_t *packet, gdrom_spi_point_type_t type,
                   uint32_t start, uint32_t end, uint8_t repeat);
void gdrom_spi_play_resume(gdrom_spi_packet_t *packet);
int gdrom_spi_seek(gdrom_spi_packet_t *packet, gdrom_spi_seek_type_t type,
                   uint32_t point);
void gdrom_spi_scan(gdrom_spi_packet_t *packet, bool reverse, uint8_t speed);

int gdrom_spi_read(gdrom_spi_packet_t *packet, uint8_t data_select,
                   gdrom_spi_expected_type_t expected,
                   gdrom_spi_point_type_t type, uint32_t start,
                   uint32_t sectors);
int gdrom_spi_read2(gdrom_spi_packet_t *packet, uint8_t data_select,
                    gdrom_spi_expected_type_t expected,
                    gdrom_spi_point_type_t type, uint32_t start,
                    uint16_t sectors, uint32_t next);
int gdrom_spi_get_subcode(gdrom_spi_packet_t *packet,
                          gdrom_spi_subcode_format_t format,
                          uint16_t allocation);
int gdrom_spi_decode_status(const uint8_t *response, size_t response_size,
                            gdrom_spi_status_t *status);
int gdrom_spi_decode_error(const uint8_t *response, size_t response_size,
                           gdrom_spi_error_t *error);
int gdrom_spi_decode_toc(const uint8_t *response, size_t response_size,
                         gdrom_spi_toc_t *toc);
int gdrom_spi_decode_session(const uint8_t *response, size_t response_size,
                             gdrom_spi_session_t *session);
int gdrom_spi_decode_mode(const uint8_t *response, size_t response_size,
                          gdrom_spi_mode_t *mode);
int gdrom_spi_encode_mode_writable(
    uint8_t output[GDROM_SPI_MODE_WRITABLE_SIZE],
    const gdrom_spi_mode_t *mode);

#endif /* __KOS_INTERNAL_GDROM_SPI_H */
