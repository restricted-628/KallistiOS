/* Host-side golden-vector tests for the GD-ROM SPI packet encoder. */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "gdrom_spi.h"

#define VECTOR(...) ((const uint8_t[GDROM_SPI_PACKET_SIZE]){__VA_ARGS__})

static void expect(const gdrom_spi_packet_t *packet, const uint8_t *vector) {
    static unsigned int vector_number;
    size_t i;

    ++vector_number;
    if(memcmp(packet->bytes, vector, GDROM_SPI_PACKET_SIZE) == 0)
        return;

    fprintf(stderr, "golden vector %u mismatch\nexpected:", vector_number);
    for(i = 0; i < GDROM_SPI_PACKET_SIZE; ++i)
        fprintf(stderr, " %02x", vector[i]);
    fprintf(stderr, "\nactual:  ");
    for(i = 0; i < GDROM_SPI_PACKET_SIZE; ++i)
        fprintf(stderr, " %02x", packet->bytes[i]);
    fputc('\n', stderr);
    abort();
}

int main(void) {
    gdrom_spi_packet_t p;
    gdrom_spi_error_t error;
    gdrom_spi_status_t status;
    gdrom_spi_toc_t toc;
    gdrom_spi_session_t session;
    gdrom_spi_mode_t mode;
    const uint8_t error_response[GDROM_SPI_ERROR_SIZE] = {
        0xf0, 0x00, 0x02, 0x00, 0x01, 0x23, 0x45, 0x67, 0x3a, 0x00
    };
    const uint8_t status_response[GDROM_SPI_STATUS_SIZE] = {
        0x01, 0x82, 0x41, 0x03, 0x07, 0x01, 0x23, 0x45, 0x08, 0x00
    };
    uint8_t toc_response[GDROM_SPI_TOC_SIZE];
    const uint8_t session_response[GDROM_SPI_SESSION_SIZE] = {
        0x01, 0x00, 0x01, 0x00, 0x10, 0x00
    };
    const uint8_t mode_response[GDROM_SPI_MODE_SIZE] = {
        0x00, 0x00, 0x02, 0x00, 0x00, 0xb4, 0x19, 0x00,
        0x00, 0x08, 'G', 'D', '-', 'R', 'O', 'M', ' ', ' ',
        '1', '.', '3', '0', ' ', ' ', ' ', ' ',
        '9', '9', '0', '1', '1', '2'
    };
    uint8_t writable_mode[GDROM_SPI_MODE_WRITABLE_SIZE];

    memset(toc_response, 0xff, sizeof(toc_response));
    memcpy(&toc_response[0],
           (const uint8_t[]){0x41, 0x00, 0x00, 0x96}, 4);
    memcpy(&toc_response[396],
           (const uint8_t[]){0x41, 0x01, 0x00, 0x00}, 4);
    memcpy(&toc_response[400],
           (const uint8_t[]){0x41, 0x01, 0x00, 0x00}, 4);
    memcpy(&toc_response[404],
           (const uint8_t[]){0x41, 0x00, 0x10, 0x00}, 4);

    gdrom_spi_test_unit(&p);
    expect(&p, VECTOR(0x00));

    gdrom_spi_open(&p);
    expect(&p, VECTOR(0x16));

    assert(gdrom_spi_req_stat(&p, 2, 8) == 0);
    expect(&p, VECTOR(0x10, 0, 2, 0, 8));
    assert(gdrom_spi_req_stat(&p, 1, 8) == EINVAL);

    assert(gdrom_spi_req_mode(&p, 10, 22) == 0);
    expect(&p, VECTOR(0x11, 0, 10, 0, 22));

    assert(gdrom_spi_set_mode(&p, 4, 6) == 0);
    expect(&p, VECTOR(0x12, 0, 4, 0, 6));

    gdrom_spi_req_error(&p, 10);
    expect(&p, VECTOR(0x13, 0, 0, 0, 10));

    gdrom_spi_get_toc(&p, false, 408);
    expect(&p, VECTOR(0x14, 0, 0, 0x01, 0x98));
    gdrom_spi_get_toc(&p, true, 408);
    expect(&p, VECTOR(0x14, 1, 0, 0x01, 0x98));

    assert(gdrom_spi_req_session(&p, 3, 6) == 0);
    expect(&p, VECTOR(0x15, 0, 3, 0, 6));
    assert(gdrom_spi_req_session(&p, 100, 6) == EINVAL);

    assert(gdrom_spi_play(&p, GDROM_SPI_POINT_FAD,
                          0x000096, 0x010203, 2) == 0);
    expect(&p, VECTOR(0x20, 1, 0, 0, 0x96, 0, 2, 0,
                      0x01, 0x02, 0x03, 0));

    assert(gdrom_spi_play(&p, GDROM_SPI_POINT_MSF,
                          0x0a0200, 0x0b3b4a, 0x0f) == 0);
    expect(&p, VECTOR(0x20, 2, 0x0a, 0x02, 0x00, 0, 0x0f, 0,
                      0x0b, 0x3b, 0x4a, 0));
    assert(gdrom_spi_play(&p, GDROM_SPI_POINT_MSF,
                          0x0a3c00, 0, 0) == EINVAL);
    assert(gdrom_spi_play(&p, GDROM_SPI_POINT_FAD,
                          0x96, 0, 0x10) == EINVAL);

    gdrom_spi_play_resume(&p);
    expect(&p, VECTOR(0x20, 7));

    assert(gdrom_spi_seek(&p, GDROM_SPI_SEEK_FAD, 0x00b06e) == 0);
    expect(&p, VECTOR(0x21, 1, 0, 0xb0, 0x6e));
    assert(gdrom_spi_seek(&p, GDROM_SPI_SEEK_STOP, 0xffffff) == 0);
    expect(&p, VECTOR(0x21, 3));
    assert(gdrom_spi_seek(&p, (gdrom_spi_seek_type_t)5, 0) == EINVAL);

    gdrom_spi_scan(&p, true, 9);
    expect(&p, VECTOR(0x22, 0, 1, 9));

    assert(gdrom_spi_read(&p, GDROM_SPI_SELECT_DATA,
                          GDROM_SPI_EXPECT_MODE1, GDROM_SPI_POINT_FAD,
                          0x00b06e, 0x010203) == 0);
    expect(&p, VECTOR(0x30, 0x24, 0, 0xb0, 0x6e, 0, 0, 0,
                      0x01, 0x02, 0x03, 0));

    assert(gdrom_spi_read(&p,
                          GDROM_SPI_SELECT_HEADER
                              | GDROM_SPI_SELECT_SUBHEADER
                              | GDROM_SPI_SELECT_DATA,
                          GDROM_SPI_EXPECT_MODE2, GDROM_SPI_POINT_MSF,
                          0x0a0210, 16) == 0);
    expect(&p, VECTOR(0x30, 0xe7, 0x0a, 0x02, 0x10, 0, 0, 0,
                      0, 0, 0x10, 0));
    assert(gdrom_spi_read(&p, 0x10, GDROM_SPI_EXPECT_ANY,
                          GDROM_SPI_POINT_FAD, 0x96, 1) == EINVAL);
    assert(gdrom_spi_read(&p, GDROM_SPI_SELECT_DATA,
                          (gdrom_spi_expected_type_t)7,
                          GDROM_SPI_POINT_FAD, 0x96, 1) == EINVAL);
    assert(gdrom_spi_read(&p, GDROM_SPI_SELECT_DATA,
                          GDROM_SPI_EXPECT_MODE1, GDROM_SPI_POINT_FAD,
                          0x96, 0x01000000) == EINVAL);

    /* Public direct-read policy: cooked Mode-1 data at FAD 150, bounded to
       sixteen sectors per packet. */
    assert(gdrom_spi_read(&p, GDROM_SPI_SELECT_DATA,
                          GDROM_SPI_EXPECT_MODE1, GDROM_SPI_POINT_FAD,
                          150, 16) == 0);
    expect(&p, VECTOR(0x30, 0x24, 0, 0, 0x96, 0, 0, 0,
                      0, 0, 0x10, 0));
    assert(gdrom_spi_read(&p, GDROM_SPI_SELECT_DATA,
                          GDROM_SPI_EXPECT_MODE2_FORM1,
                          GDROM_SPI_POINT_FAD, 150, 16) == 0);
    expect(&p, VECTOR(0x30, 0x28, 0, 0, 0x96, 0, 0, 0,
                      0, 0, 0x10, 0));

    assert(gdrom_spi_read2(&p, GDROM_SPI_SELECT_DATA,
                           GDROM_SPI_EXPECT_MODE1, GDROM_SPI_POINT_FAD,
                           0x00b06e, 16, 0x00b07e) == 0);
    expect(&p, VECTOR(0x31, 0x24, 0, 0xb0, 0x6e, 0, 0, 0x10,
                      0, 0xb0, 0x7e, 0));
    assert(gdrom_spi_read2(&p, GDROM_SPI_SELECT_DATA,
                           GDROM_SPI_EXPECT_MODE1, GDROM_SPI_POINT_MSF,
                           0x0a0210, 16, 0x0a3c00) == EINVAL);

    assert(gdrom_spi_get_subcode(&p, GDROM_SPI_SUBCODE_Q, 14) == 0);
    expect(&p, VECTOR(0x40, 1, 0, 0, 14));
    assert(gdrom_spi_get_subcode(&p,
                                 (gdrom_spi_subcode_format_t)4, 14) == EINVAL);

    assert(gdrom_spi_decode_status(status_response,
                                   sizeof(status_response), &status) == 0);
    assert(status.status == 0x01);
    assert(status.disc_format == 0x80);
    assert(status.repeat_count == 0x02);
    assert(status.adr == 0x04);
    assert(status.control == 0x01);
    assert(status.track == 0x03);
    assert(status.index == 0x07);
    assert(status.fad == 0x012345);
    assert(status.max_read_retries == 0x08);
    assert(gdrom_spi_decode_status(status_response,
                                   sizeof(status_response) - 1, &status)
           == EINVAL);
    assert(gdrom_spi_decode_status(NULL, sizeof(status_response), &status)
           == EINVAL);

    {
        uint8_t malformed[GDROM_SPI_STATUS_SIZE];

        memcpy(malformed, status_response, sizeof(malformed));
        malformed[9] = 1;
        assert(gdrom_spi_decode_status(malformed, sizeof(malformed), &status)
               == EPROTO);
    }

    assert(gdrom_spi_decode_error(error_response,
                                  sizeof(error_response), &error) == 0);
    assert(error.sense_key == 0x02);
    assert(error.command_specific_information == 0x01234567);
    assert(error.asc == 0x3a);
    assert(error.ascq == 0x00);
    assert(gdrom_spi_decode_error(error_response,
                                  sizeof(error_response) - 1, &error)
           == EINVAL);
    assert(gdrom_spi_decode_error(NULL, sizeof(error_response), &error)
           == EINVAL);

    {
        uint8_t malformed[GDROM_SPI_ERROR_SIZE];

        memcpy(malformed, error_response, sizeof(malformed));
        malformed[0] = 0x70;
        assert(gdrom_spi_decode_error(malformed, sizeof(malformed), &error)
               == EPROTO);
    }

    assert(gdrom_spi_decode_toc(toc_response, sizeof(toc_response), &toc)
           == 0);
    assert(toc.entry[0] == 0x41000096u);
    assert(toc.entry[1] == 0xffffffffu);
    assert(toc.first == 0x41010000u);
    assert(toc.last == 0x41010000u);
    assert(toc.leadout == 0x41001000u);
    assert(gdrom_spi_decode_toc(toc_response,
                                sizeof(toc_response) - 1, &toc) == EINVAL);
    toc_response[398] = 1;
    assert(gdrom_spi_decode_toc(toc_response, sizeof(toc_response), &toc)
           == EPROTO);

    assert(gdrom_spi_decode_session(session_response,
                                    sizeof(session_response), &session) == 0);
    assert(session.status == 1);
    assert(session.number_or_track == 1);
    assert(session.fad == 0x001000u);
    assert(gdrom_spi_decode_session(session_response,
                                    sizeof(session_response) - 1,
                                    &session) == EINVAL);

    assert(gdrom_spi_decode_mode(mode_response,
                                 sizeof(mode_response), &mode) == 0);
    assert(mode.speed == 2);
    assert(mode.standby_seconds == 180);
    assert(!mode.read_continuous);
    assert(mode.ecc_retry);
    assert(mode.read_retry);
    assert(mode.form2_retry);
    assert(mode.read_retry_count == 8);
    assert(memcmp(mode.drive_information, "GD-ROM  ", 8) == 0);
    assert(memcmp(mode.system_version, "1.30    ", 8) == 0);
    assert(memcmp(mode.system_date, "990112", 6) == 0);
    assert(gdrom_spi_decode_mode(mode_response,
                                 sizeof(mode_response) - 1, &mode) == EINVAL);

    assert(gdrom_spi_encode_mode_writable(writable_mode, &mode) == 0);
    assert(memcmp(writable_mode,
                  (const uint8_t[]){2, 0, 0, 0xb4, 0x19, 0, 0, 8},
                  sizeof(writable_mode)) == 0);
    mode.speed = 8;
    assert(gdrom_spi_encode_mode_writable(writable_mode, &mode) == EINVAL);

    {
        uint8_t malformed[GDROM_SPI_MODE_SIZE];

        memcpy(malformed, mode_response, sizeof(malformed));
        malformed[6] |= 0x02;
        assert(gdrom_spi_decode_mode(malformed, sizeof(malformed), &mode)
               == EPROTO);
    }

    return 0;
}
