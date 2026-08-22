/* KallistiOS ##version##

   hardware/gdrom_spi.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <string.h>

#include "gdrom_spi.h"

static void packet_init(gdrom_spi_packet_t *packet, gdrom_spi_opcode_t opcode) {
    memset(packet, 0, sizeof(*packet));
    packet->bytes[0] = (uint8_t)opcode;
}

static void put_u16be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static void put_u24be(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 16);
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)value;
}

static uint32_t get_u24be(const uint8_t *in) {
    return ((uint32_t)in[0] << 16) | ((uint32_t)in[1] << 8) | in[2];
}

static uint32_t get_toc_entry(const uint8_t *in) {
    return ((uint32_t)in[0] << 24) | get_u24be(&in[1]);
}

static bool valid_msf(uint32_t point) {
    return ((point >> 8) & 0xffu) < 60u && (point & 0xffu) < 75u;
}

static bool valid_point(gdrom_spi_point_type_t type, uint32_t point) {
    if(point > GDROM_SPI_MAX_U24)
        return false;

    return type == GDROM_SPI_POINT_FAD
        || (type == GDROM_SPI_POINT_MSF && valid_msf(point));
}

static int req_window(gdrom_spi_packet_t *packet, gdrom_spi_opcode_t opcode,
                      uint8_t start, uint8_t allocation) {
    if(start & 1u)
        return EINVAL;

    packet_init(packet, opcode);
    packet->bytes[2] = start;
    packet->bytes[4] = allocation;
    return 0;
}

static bool valid_read_fields(uint8_t data_select,
                              gdrom_spi_expected_type_t expected,
                              gdrom_spi_point_type_t type, uint32_t start) {
    return data_select <= 0x0fu && expected <= GDROM_SPI_EXPECT_MODE2_NON_XA
        && valid_point(type, start);
}

static uint8_t read_flags(uint8_t data_select,
                          gdrom_spi_expected_type_t expected,
                          gdrom_spi_point_type_t type) {
    return (uint8_t)((data_select << 4) | ((uint8_t)expected << 1)
                     | (type == GDROM_SPI_POINT_MSF));
}

void gdrom_spi_test_unit(gdrom_spi_packet_t *packet) {
    packet_init(packet, GDROM_SPI_TEST_UNIT);
}

void gdrom_spi_open(gdrom_spi_packet_t *packet) {
    packet_init(packet, GDROM_SPI_CD_OPEN);
}

int gdrom_spi_req_stat(gdrom_spi_packet_t *packet, uint8_t start,
                       uint8_t allocation) {
    return req_window(packet, GDROM_SPI_REQ_STAT, start, allocation);
}

int gdrom_spi_req_mode(gdrom_spi_packet_t *packet, uint8_t start,
                       uint8_t allocation) {
    return req_window(packet, GDROM_SPI_REQ_MODE, start, allocation);
}

int gdrom_spi_set_mode(gdrom_spi_packet_t *packet, uint8_t start,
                       uint8_t allocation) {
    return req_window(packet, GDROM_SPI_SET_MODE, start, allocation);
}

void gdrom_spi_req_error(gdrom_spi_packet_t *packet, uint8_t allocation) {
    packet_init(packet, GDROM_SPI_REQ_ERROR);
    packet->bytes[4] = allocation;
}

void gdrom_spi_get_toc(gdrom_spi_packet_t *packet, bool high_density,
                       uint16_t allocation) {
    packet_init(packet, GDROM_SPI_GET_TOC);
    packet->bytes[1] = high_density ? 1u : 0u;
    put_u16be(&packet->bytes[3], allocation);
}

int gdrom_spi_req_session(gdrom_spi_packet_t *packet, uint8_t session,
                          uint8_t allocation) {
    if(session > 99u)
        return EINVAL;

    packet_init(packet, GDROM_SPI_REQ_SES);
    packet->bytes[2] = session;
    packet->bytes[4] = allocation;
    return 0;
}

int gdrom_spi_play(gdrom_spi_packet_t *packet, gdrom_spi_point_type_t type,
                   uint32_t start, uint32_t end, uint8_t repeat) {
    if(!valid_point(type, start) || !valid_point(type, end) || repeat > 0x0fu)
        return EINVAL;

    packet_init(packet, GDROM_SPI_CD_PLAY);
    packet->bytes[1] = (uint8_t)type;
    put_u24be(&packet->bytes[2], start);
    packet->bytes[6] = repeat;
    put_u24be(&packet->bytes[8], end);
    return 0;
}

void gdrom_spi_play_resume(gdrom_spi_packet_t *packet) {
    packet_init(packet, GDROM_SPI_CD_PLAY);
    packet->bytes[1] = 7u;
}

int gdrom_spi_seek(gdrom_spi_packet_t *packet, gdrom_spi_seek_type_t type,
                   uint32_t point) {
    if(type < GDROM_SPI_SEEK_FAD || type > GDROM_SPI_SEEK_PAUSE)
        return EINVAL;
    if(type <= GDROM_SPI_SEEK_MSF
            && !valid_point((gdrom_spi_point_type_t)type, point))
        return EINVAL;

    packet_init(packet, GDROM_SPI_CD_SEEK);
    packet->bytes[1] = (uint8_t)type;
    if(type <= GDROM_SPI_SEEK_MSF)
        put_u24be(&packet->bytes[2], point);
    return 0;
}

void gdrom_spi_scan(gdrom_spi_packet_t *packet, bool reverse, uint8_t speed) {
    packet_init(packet, GDROM_SPI_CD_SCAN);
    packet->bytes[2] = reverse ? 1u : 0u;
    packet->bytes[3] = speed;
}

int gdrom_spi_read(gdrom_spi_packet_t *packet, uint8_t data_select,
                   gdrom_spi_expected_type_t expected,
                   gdrom_spi_point_type_t type, uint32_t start,
                   uint32_t sectors) {
    if(!valid_read_fields(data_select, expected, type, start)
            || sectors > GDROM_SPI_MAX_U24)
        return EINVAL;

    packet_init(packet, GDROM_SPI_CD_READ);
    packet->bytes[1] = read_flags(data_select, expected, type);
    put_u24be(&packet->bytes[2], start);
    put_u24be(&packet->bytes[8], sectors);
    return 0;
}

int gdrom_spi_read2(gdrom_spi_packet_t *packet, uint8_t data_select,
                    gdrom_spi_expected_type_t expected,
                    gdrom_spi_point_type_t type, uint32_t start,
                    uint16_t sectors, uint32_t next) {
    if(!valid_read_fields(data_select, expected, type, start)
            || !valid_point(type, next))
        return EINVAL;

    packet_init(packet, GDROM_SPI_CD_READ2);
    packet->bytes[1] = read_flags(data_select, expected, type);
    put_u24be(&packet->bytes[2], start);
    put_u16be(&packet->bytes[6], sectors);
    put_u24be(&packet->bytes[8], next);
    return 0;
}

int gdrom_spi_get_subcode(gdrom_spi_packet_t *packet,
                          gdrom_spi_subcode_format_t format,
                          uint16_t allocation) {
    if(format < GDROM_SPI_SUBCODE_RAW || format > GDROM_SPI_SUBCODE_ISRC)
        return EINVAL;

    packet_init(packet, GDROM_SPI_GET_SCD);
    packet->bytes[1] = (uint8_t)format;
    put_u16be(&packet->bytes[3], allocation);
    return 0;
}

int gdrom_spi_decode_status(const uint8_t *response, size_t response_size,
                            gdrom_spi_status_t *status) {
    if(!response || !status || response_size != GDROM_SPI_STATUS_SIZE)
        return EINVAL;
    if((response[0] & 0xf0u) || response[9])
        return EPROTO;

    status->status = response[0] & 0x0f;
    status->disc_format = response[1] & 0xf0;
    status->repeat_count = response[1] & 0x0f;
    status->adr = response[2] >> 4;
    status->control = response[2] & 0x0f;
    status->track = response[3];
    status->index = response[4];
    status->fad = ((uint32_t)response[5] << 16)
        | ((uint32_t)response[6] << 8) | response[7];
    status->max_read_retries = response[8];
    return 0;
}

int gdrom_spi_decode_error(const uint8_t *response, size_t response_size,
                           gdrom_spi_error_t *error) {
    if(!response || !error || response_size != GDROM_SPI_ERROR_SIZE)
        return EINVAL;
    if(response[0] != 0xf0u || response[1]
            || (response[2] & 0xf0u) || response[3])
        return EPROTO;

    error->sense_key = response[2] & 0x0fu;
    error->command_specific_information = ((uint32_t)response[4] << 24)
        | ((uint32_t)response[5] << 16)
        | ((uint32_t)response[6] << 8) | response[7];
    error->asc = response[8];
    error->ascq = response[9];
    return 0;
}

int gdrom_spi_decode_toc(const uint8_t *response, size_t response_size,
                         gdrom_spi_toc_t *toc) {
    size_t i;

    if(!response || !toc || response_size != GDROM_SPI_TOC_SIZE)
        return EINVAL;
    if(response[398] || response[399]
            || response[402] || response[403])
        return EPROTO;
    if(response[397] < 1u || response[397] > 99u
            || response[401] < response[397] || response[401] > 99u)
        return EPROTO;

    for(i = 0; i < 99; ++i)
        toc->entry[i] = get_toc_entry(&response[i * 4]);
    toc->first = get_toc_entry(&response[396]);
    toc->last = get_toc_entry(&response[400]);
    toc->leadout = get_toc_entry(&response[404]);
    return 0;
}

int gdrom_spi_decode_session(const uint8_t *response, size_t response_size,
                             gdrom_spi_session_t *session) {
    if(!response || !session || response_size != GDROM_SPI_SESSION_SIZE)
        return EINVAL;
    if((response[0] & 0xf0u) || response[1]
            || response[2] < 1u || response[2] > 99u)
        return EPROTO;

    session->status = response[0] & 0x0fu;
    session->number_or_track = response[2];
    session->fad = get_u24be(&response[3]);
    return 0;
}

int gdrom_spi_decode_mode(const uint8_t *response, size_t response_size,
                          gdrom_spi_mode_t *mode) {
    if(!response || !mode || response_size != GDROM_SPI_MODE_SIZE)
        return EINVAL;
    if(response[0] || response[1] || response[2] > 7u || response[3]
            || (response[6] & 0xc6u) || response[7] || response[8])
        return EPROTO;

    mode->speed = response[2];
    mode->standby_seconds = ((uint16_t)response[4] << 8) | response[5];
    mode->read_continuous = (response[6] & 0x20u) != 0;
    mode->ecc_retry = (response[6] & 0x10u) != 0;
    mode->read_retry = (response[6] & 0x08u) != 0;
    mode->form2_retry = (response[6] & 0x01u) != 0;
    mode->read_retry_count = response[9];
    memcpy(mode->drive_information, &response[10],
           sizeof(mode->drive_information));
    memcpy(mode->system_version, &response[18],
           sizeof(mode->system_version));
    memcpy(mode->system_date, &response[26], sizeof(mode->system_date));
    return 0;
}

int gdrom_spi_encode_mode_writable(
        uint8_t output[GDROM_SPI_MODE_WRITABLE_SIZE],
        const gdrom_spi_mode_t *mode) {
    if(!output || !mode || mode->speed > 7u)
        return EINVAL;

    output[0] = mode->speed;
    output[1] = 0;
    put_u16be(&output[2], mode->standby_seconds);
    output[4] = (mode->read_continuous ? 0x20u : 0)
        | (mode->ecc_retry ? 0x10u : 0)
        | (mode->read_retry ? 0x08u : 0)
        | (mode->form2_retry ? 0x01u : 0);
    output[5] = 0;
    output[6] = 0;
    output[7] = mode->read_retry_count;
    return 0;
}
