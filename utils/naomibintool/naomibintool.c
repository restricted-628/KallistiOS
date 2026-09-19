/* KallistiOS ##version##

   naomibintool.c
   Copyright (C) 2020 Lawrence Sebald

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef NO_LIBELF
#include <libelf.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif

#define NAOMI_REGION_JAPAN          0
#define NAOMI_REGION_USA            1
#define NAOMI_REGION_EXPORT         2
#define NAOMI_REGION_KOREA          3
#define NAOMI_REGION_AUSTRALIA      4

#define DEFAULT_PLATFORM  "NAOMI           "
#define DEFAULT_DEVELOPER "Anonymous Developer             "
#define DEFAULT_TITLE     "Homebrew Application            "
#define DEFAULT_SEQ1      "CREDIT TO START                 "
#define DEFAULT_SEQ2      "CREDIT TO CONTINUE              "

typedef struct naomi_segment {
    uint32_t rom_offset;
    uint32_t ram_offset;
    uint32_t size;
} naomi_segment_t;

/* NAOMI ROM header structure. Thanks to DragonMinded's documentation for the
   data here (https://github.com/DragonMinded/netboot/blob/trunk/docs/naomi.md)
 */
typedef struct naomi_hdr {
    char signature[16];
    char developer[32];
    char region_title[8][32];
    uint16_t mfg_year;
    uint8_t mfg_month;
    uint8_t mfg_day;
    char serial_number[4];
    uint16_t eightmb_mode;
    uint16_t g1_init;
    uint32_t g1_rrc;
    uint32_t g1_rwc;
    uint32_t g1_frc;
    uint32_t g1_fwc;
    uint32_t g1_crc;
    uint32_t g1_cwc;
    uint32_t g1_gdrc;
    uint32_t g1_gdwc;
    uint8_t m2m4_checksum[132];
    struct {
        uint8_t apply;
        uint8_t system_settings;
        uint8_t coin_chute;
        uint8_t coin_setting;
        uint8_t coin1_rate;
        uint8_t coin2_rate;
        uint8_t credit_rate;
        uint8_t bonus_rate;
        uint8_t seqtext_offset[8];
    } eeprom[8];
    char sequence_text[8][32];
    naomi_segment_t segment[8];
    naomi_segment_t test_segment[8];
    uint32_t entry;
    uint32_t test_entry;
    uint8_t supported_regions;
    uint8_t supported_players;
    uint8_t supported_display_freq;
    uint8_t supported_display_dir;
    uint8_t check_eeprom;
    uint8_t service_type;
    uint8_t m1_checksums[138];
    uint8_t padding[71];
    uint8_t encrypted;
} naomi_hdr_t;

static void print_header(const naomi_hdr_t *hdr) {
    int i;

    printf("Platform Signature: %.16s\n", hdr->signature);
    printf("Developer: %.32s\n", hdr->developer);

    for(i = 0; i < 8; ++i) {
        printf("Region title %d: %.32s\n", i + 1, hdr->region_title[i]);
    }
}

static void print_segments(const naomi_hdr_t *hdr) {
    int i;

    for(i = 0; i < 8; ++i) {
        if(hdr->segment[i].rom_offset == 0xFFFFFFFF)
            break;

        printf("Segment %d\n"
               "ROM Offset: %08" PRIx32 "\n"
               "RAM Offset: %08" PRIx32 "\n"
               "Size: %" PRIu32 "\n", i + 1, hdr->segment[i].rom_offset,
               hdr->segment[i].ram_offset, hdr->segment[i].size);
    }

    for(i = 0; i < 8; ++i) {
        if(hdr->test_segment[i].rom_offset == 0xFFFFFFFF)
            break;

        printf("Test Segment %d\n"
               "ROM Offset: %08" PRIx32 "\n"
               "RAM Offset: %08" PRIx32 "\n"
               "Size: %" PRIu32 "\n", i + 1, hdr->test_segment[i].rom_offset,
               hdr->test_segment[i].ram_offset, hdr->test_segment[i].size);
    }
}

static void print_entries(const naomi_hdr_t *hdr) {
    printf("Entry point: %08" PRIx32 "\n"
           "Test Entry point: %08" PRIx32 "\n", hdr->entry, hdr->test_entry);
}

void usage(const char *progname) {
    printf("Usage: %s oper filename [args]\n\n", progname);
    printf("Where oper is one of the following:\n"
           "  read  -- Reads the ROM header and prints out information.\n"
           "  build -- Builds a ROM from the specified arguments.\n\n"
           "Arguments for the build operation:\n"
           "  -p name        - Specify the platform for the rom\n"
           "                   (default: \"NAOMI\").\n"
           "  -d name        - Specify the developer of the rom.\n"
           "  -t name[:regn] - Specify the title of the rom for a the given\n"
           "                   region number. If no number is given, the\n"
           "                   title is used for all regions.\n"
           "  -b file[:addr] - Specify a binary to pack into rom, and\n"
           "                   optionally the address to load to. The default\n"
           "                   address is 0x8c020000.\n"
           "  -s addr        - Specify the entry point address\n"
           "                   (default: 0x8c020000).\n"
#ifndef NO_LIBELF
           "  -e file        - Specify an ELF binary to pack into rom. The\n"
           "                   load address will be detected automatically,\n"
           "                   as will the entry point.\n"
#endif
           "Note: Currently only one bin can be packed into a rom.\n"
           "This will be fixed in a future version of this tool.\n");
}

int read_header(int argc, char *argv[]) {
    FILE *fp;
    naomi_hdr_t hdr;

    if(argc != 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if(!(fp = fopen(argv[2], "rb"))) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    if(fread(&hdr, sizeof(naomi_hdr_t), 1, fp) != 1) {
        perror("Error reading file");
        fclose(fp);
        return EXIT_FAILURE;
    }

    fclose(fp);

    if(memcmp(hdr.signature, "NAOMI           ", 16) &&
       memcmp(hdr.signature, "Naomi2          ", 16)) {
        fprintf(stderr, "File does not appear to be a NAOMI/NAOMI2 ROM.\n");
        return EXIT_FAILURE;
    }

    print_header(&hdr);
    print_segments(&hdr);
    print_entries(&hdr);

    return 0;
}

static long get_filesize(FILE *fp) {
    long len;

    if(fseek(fp, 0, SEEK_END) < 0) {
        perror("Cannot read binary");
        return EXIT_FAILURE;
    }

    len = ftell(fp);

    if(fseek(fp, 0, SEEK_SET) < 0) {
        perror("Cannot read binary");
        return EXIT_FAILURE;
    }

    return len;
}

#define BUF_SIZE 4096
static uint8_t buf[BUF_SIZE];

static int write_rom_bin(naomi_hdr_t *hdr, const char fn[], FILE *bin) {
    FILE *fp;
    size_t sz, sz2;

    if(!(fp = fopen(fn, "wb"))) {
        perror("Cannot open file for writing");
        return EXIT_FAILURE;
    }

    if(fwrite(hdr, sizeof(naomi_hdr_t), 1, fp) != 1) {
        perror("Cannot write file");
        fclose(fp);
        return EXIT_FAILURE;
    }

    if(fseek(fp, 0x1000, SEEK_SET) < 0) {
        perror("Cannot write file");
        fclose(fp);
        return EXIT_FAILURE;
    }

    do {
        sz = fread(buf, 1, BUF_SIZE, bin);
        if(!sz) {
            if(ferror(bin)) {
                perror("Cannot read binary");
                fclose(fp);
                return EXIT_FAILURE;
            }
        }
        else {
            sz2 = fwrite(buf, 1, sz, fp);
            if(!sz2) {
                perror("Cannot write file");
                fclose(fp);
                return EXIT_FAILURE;
            }
        }
    } while(sz);

    fclose(fp);

    printf("Successfully wrote rom file.\n");
    return 0;
}

#ifndef NO_LIBELF
static int write_rom_elf(naomi_hdr_t *hdr, const char fn[], int bin) {
    FILE *fp;
    size_t sz, sz2;
    Elf *e;
    Elf32_Ehdr *ehdr;
    Elf32_Shdr *shdr;
    Elf_Scn *section = NULL;
    Elf_Data *data;
    Elf32_Word arch;
    size_t index;
    char *sn;
    uint32_t base = 0;
    long off;

    if(elf_version(EV_CURRENT) == EV_NONE) {
        fprintf(stderr, "Error initializing libelf: %s\n", elf_errmsg(-1));
        return EXIT_FAILURE;
    }

    if((e = elf_begin(bin, ELF_C_READ, NULL)) == NULL) {
        fprintf(stderr, "Error reading ELF: %s\n", elf_errmsg(-1));
        return EXIT_FAILURE;
    }

    if(elf_kind(e) != ELF_K_ELF) {
        fprintf(stderr, "Specified file is not an ELF binary.\n");
        elf_end(e);
        return EXIT_FAILURE;
    }

    if(!(ehdr = elf32_getehdr(e))) {
        fprintf(stderr, "Unable to read ELF32 header: %s\n", elf_errmsg(-1));
        elf_end(e);
        return EXIT_FAILURE;
    }

    if(ehdr->e_machine != EM_SH) {
        fprintf(stderr, "Binary is not a SuperH ELF file.\n");
        elf_end(e);
        return EXIT_FAILURE;
    }

    if(ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "Binary is not a 32-bit ELF.\n");
        elf_end(e);
        return EXIT_FAILURE;
    }

    if(ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "Binary is not little endian.\n");
        elf_end(e);
        return EXIT_FAILURE;
    }

    arch = ehdr->e_flags & EF_SH_MACH_MASK;
    if(arch != EF_SH4 && arch != EF_SH4_NOFPU && arch != EF_SH_UNKNOWN &&
       arch != EF_SH4A && arch != EF_SH4A_NOFPU && arch != EF_SH4_NOMMU_NOFPU) {
        fprintf(stderr, "Binary is not compiled for SH4.\n");
        elf_end(e);
        return EXIT_FAILURE;
    }

    if(!(fp = fopen(fn, "wb"))) {
        perror("Cannot open file for writing");
        return EXIT_FAILURE;
    }

    if(fseek(fp, 0x1000, SEEK_SET) < 0) {
        perror("Cannot write file");
        fclose(fp);
        return EXIT_FAILURE;
    }

    hdr->entry = ehdr->e_entry;
    hdr->test_entry = ehdr->e_entry;
    printf("Entry point is 0x%08" PRIx32 "\n", hdr->entry);

    if(elf_getshdrstrndx(e, &index)) {
        fprintf(stderr, "Unable to read section index: %s\n", elf_errmsg(-1));
        elf_end(e);
        fclose(fp);
        return EXIT_FAILURE;
    }

    /* Go through each section and add it to the binary. */
    while((section = elf_nextscn(e, section))) {
        if(!(shdr = elf32_getshdr(section))) {
            fprintf(stderr, "Unable to read section header: %s\n",
                    elf_errmsg(-1));
            elf_end(e);
            fclose(fp);
            return EXIT_FAILURE;
        }

        if(!(sn = elf_strptr(e, index, shdr->sh_name))) {
            fprintf(stderr, "Unable to read section name: %s\n",
                    elf_errmsg(-1));
            elf_end(e);
            fclose(fp);
            return EXIT_FAILURE;
        }

        if(!shdr->sh_addr)
            continue;

        data = elf_getdata(section, NULL);
        if(!data->d_buf || !data->d_size)
            continue;

        printf("Section %-20s Address: 0x%08" PRIx32 ", size: %d\n",
               sn, shdr->sh_addr, shdr->sh_size);

        if(base == 0) {
            base = shdr->sh_addr;
            hdr->segment[0].ram_offset = base;
            hdr->test_segment[0].ram_offset = base;
        }

        if(shdr->sh_addr < base) {
            fprintf(stderr, "Section has invalid address\n");
            elf_end(e);
            fclose(fp);
            return EXIT_FAILURE;
        }

        /* Move to the right place in our output binary... */
        if(fseek(fp, 0x1000 + shdr->sh_addr - base, SEEK_SET) < 0) {
            perror("Cannot write file");
            elf_end(e);
            fclose(fp);
            return EXIT_FAILURE;
        }

        do {
            if(fwrite(data->d_buf, 1, data->d_size, fp) != data->d_size) {
                fprintf(stderr, "Cannot write file.\n");
                elf_end(e);
                fclose(fp);
                return EXIT_FAILURE;
            }
        } while((data = elf_getdata(section, data)));
    }

    elf_end(e);

    /* Figure out how much we wrote into the binary. */
    off = ftell(fp) - 0x1000;
    hdr->segment[0].rom_offset = 0x1000;
    hdr->segment[0].ram_offset = base;
    hdr->segment[0].size = (uint32_t)off;
    hdr->test_segment[0].rom_offset = 0x1000;
    hdr->test_segment[0].ram_offset = base;
    hdr->test_segment[0].size = (uint32_t)off;

    /* Rewind to the beginning to write the header. */
    if(fseek(fp, 0, SEEK_SET) < 0) {
        perror("Cannot write file");
        fclose(fp);
        return EXIT_FAILURE;
    }

    if(fwrite(hdr, sizeof(naomi_hdr_t), 1, fp) != 1) {
        perror("Cannot write file");
        fclose(fp);
        return EXIT_FAILURE;
    }

    fclose(fp);

    printf("Successfully wrote rom file.\n");
    return 0;
}
#endif /* !NO_LIBELF */

int build_rom(int argc, char *argv[]) {
    char *argv2[argc - 2];
    int i, j, k;
    naomi_hdr_t hdr;
    char *tmp, *tmp2;
    FILE *binfile = NULL;
    int elffile = -1;

    /* Clear the header and set some reasonable defaults... */
    memset(&hdr, 0, sizeof(naomi_hdr_t));
    memcpy(hdr.signature, DEFAULT_PLATFORM, 16);
    memcpy(hdr.developer, DEFAULT_DEVELOPER, 32);
    for(i = 0; i < 8; ++i) {
        memcpy(hdr.region_title[i], DEFAULT_TITLE, 32);
    }

    hdr.mfg_year = 1999;
    hdr.mfg_month = 9;
    hdr.mfg_day = 9;

    hdr.serial_number[0] = hdr.serial_number[1] = hdr.serial_number[2] =
        hdr.serial_number[3] = 'X';

    memcpy(hdr.sequence_text[0], DEFAULT_SEQ1, 32);
    memcpy(hdr.sequence_text[1], DEFAULT_SEQ2, 32);

    hdr.entry = 0x8c020000;
    hdr.test_entry = 0x8c020000;
    hdr.segment[0].rom_offset = 0x1000;
    hdr.segment[0].ram_offset = 0x8c020000;
    hdr.segment[1].rom_offset = 0xFFFFFFFF;
    hdr.test_segment[0].rom_offset = 0x1000;
    hdr.test_segment[0].ram_offset = 0x8c020000;
    hdr.test_segment[1].rom_offset = 0xFFFFFFFF;
    hdr.supported_regions = 0xFF;
    memset(hdr.padding, 0xFF, 71);
    hdr.encrypted = 0xFF;

    /* Shift off the "build" command and the filename. */
    argv2[0] = argv[0];
    for(i = 3; i < argc; ++i) {
        argv2[i - 2] = argv[i];
    }

    /* Parse arguments. */
    while((i = getopt(argc - 2, argv2, ":p:d:t:b:s:e:")) != -1) {
        switch(i) {
            case 'p':
                j = strlen(optarg);
                if(j > 16) {
                    fprintf(stderr, "Invalid platform name: '%s'\n", optarg);
                    goto err;
                }

                memcpy(hdr.signature, optarg, j);
                while(j < 16)
                    hdr.signature[j++] = ' ';
                break;

            case 'd':
                j = strlen(optarg);
                if(j > 32) {
                    fprintf(stderr, "Invalid developer name: '%s'\n", optarg);
                    goto err;
                }

                memcpy(hdr.developer, optarg, j);
                while(j < 32)
                    hdr.developer[j++] = ' ';
                break;

            case 't':
                /* Do we have a region number? */
                tmp = strdup(optarg);
                k = -1;

                if((tmp2 = strchr(tmp, ':'))) {
                    *tmp2++ = 0;
                    errno = 0;
                    k = (int)strtol(tmp2, NULL, 10);

                    if(errno || k < 0 || k > 7) {
                        fprintf(stderr, "Invalid region number: '%s'\n", tmp2);
                        free(tmp);
                        goto err;
                    }
                }

                j = strlen(tmp);
                if(j > 32) {
                    fprintf(stderr, "Invalid title: '%s'\n", tmp);
                    free(tmp);
                    goto err;
                }

                if(k != -1) {
                    memcpy(hdr.region_title[k], tmp, j);
                    while(j < 32)
                        hdr.region_title[k][j++] = ' ';
                }
                else {
                    memcpy(hdr.region_title[0], tmp, j);
                    while(j < 32)
                        hdr.region_title[0][j++] = ' ';

                    for(k = 1; k < 8; ++k) {
                        memcpy(hdr.region_title[k], hdr.region_title[0], 32);
                    }
                }

                free(tmp);
                break;

            case 's':
                errno = 0;
                hdr.entry = (uint32_t)strtoul(optarg, NULL, 16);

                if(errno) {
                    fprintf(stderr, "Invalid entry point: '%s'", optarg);
                    goto err;
                }

                hdr.test_entry = hdr.entry;
                break;

            case 'b':
                if(binfile || elffile >= 0) {
                    fprintf(stderr, "Cannot load multiple binaries!\n");
                    goto err;
                }

                /* Do we have a load address? */
                tmp = strdup(optarg);
                if((tmp2 = strchr(tmp, ':'))) {
                    *tmp2 = 0;
                    errno = 0;
                    hdr.segment[0].ram_offset =
                        (uint32_t)strtoul(tmp2, NULL, 16);
                    hdr.test_segment[0].ram_offset = hdr.segment[0].ram_offset;

                    if(errno) {
                        fprintf(stderr, "Invalid load address: '%s'\n", tmp2);
                        free(tmp);
                        return EXIT_FAILURE;
                    }
                }

                binfile = fopen(tmp, "rb");
                free(tmp);
                if(!binfile) {
                    perror("Cannot open binary");
                    return EXIT_FAILURE;
                }

                hdr.segment[0].size = (uint32_t)get_filesize(binfile);
                hdr.test_segment[0].size = hdr.segment[0].size;
                break;

            case 'e':
#ifdef NO_LIBELF
                fprintf(stderr, "-e option requires libelf.\n");
                return EXIT_FAILURE;
#else
                if(binfile || elffile >= 0) {
                    fprintf(stderr, "Cannot load multiple binaries!\n");
                    goto err;
                }

                elffile = open(optarg, O_BINARY | O_RDONLY);
                if(elffile < 0) {
                    perror("Cannot open binary");
                    return EXIT_FAILURE;
                }

                break;
#endif

            case '?':
                fprintf(stderr, "Unrecognized option: '-%c'\n", optopt);
                goto err;

            case ':':
                fprintf(stderr, "Option -%c requires an argument\n", optopt);
                goto err;
        }
    }

    /* Did we get a binary? */
    if(!binfile && elffile < 0) {
        fprintf(stderr, "You must specify a binary to pack into the rom!\n");
        return EXIT_FAILURE;
    }

    /* Write out our binary... */
    if(binfile) {
        i = write_rom_bin(&hdr, argv[2], binfile);
        fclose(binfile);
    }
#ifndef NO_LIBELF
    else {
        i = write_rom_elf(&hdr, argv[2], elffile);
        close(elffile);
    }
#endif

    return i;

err:
    if(binfile)
        fclose(binfile);

    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    if(argc < 3) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if(!strcmp(argv[1], "read")) {
        return read_header(argc, argv);
    }
    else if(!strcmp(argv[1], "build")) {
        return build_rom(argc, argv);
    }

    usage(argv[0]);
    return EXIT_FAILURE;
}
