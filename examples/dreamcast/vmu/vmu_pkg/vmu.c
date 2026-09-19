/* KallistiOS ##version##

   vmu.c
   (c)2002 Megan Potter
   Copyright (C) 2026 Joseph Black
*/

/* This simple example shows how to use the vmu_pkg_* functions to write
   a file to a VMU with a DC-compatible header so it can be managed like
   any other VMU file from the BIOS menus. */

#include <kos.h>
#include <errno.h>

/* An icon is always 32x32 4bpp */
#define ICON_SIZE (32 * 32 / 2)

#define NB_ICONS_MAX 3

/* How many bytes of data to write */
#define DATA_LEN 4096

#define SCREEN_W 640
#define SCREEN_H 480

/* The Y indentation for the VMU Info text on screen */
#define INFO_Y 88

/* The amount of space from the top of one row of text to the next */
#define ROW_SPACER 24

void draw_dir(void) {
    file_t      d;
    size_t      y = INFO_Y;
    const dirent_t    *de;

    d = fs_open("/vmu/a1", O_RDONLY | O_DIR);

    /* If fs_open returned an error */
    if(d == FILEHND_INVALID) {
        bfont_draw_str(vram_s + y * SCREEN_W + 10, SCREEN_W, 0, "Can't read VMU");
        return;
    }

    /* Since there was no error, read through the files */
    while((de = fs_readdir(d))) {
        bfont_draw_str(vram_s + y * SCREEN_W + 10, SCREEN_W, 0, de->name);
        y += ROW_SPACER;

        /* If we would go off the screen, stop! */
        if(y >= (SCREEN_H - ROW_SPACER))
            break;
    }

    fs_close(d);
}

/* Clears out the portion of the screen we use to write info to */
void clear_screen_info(void) {
    memset(vram_s + INFO_Y * SCREEN_W, 0, SCREEN_W * (SCREEN_H - 64) * 2);
}

bool dev_found = false;
void new_vmu(void) {
    maple_device_t *dev;

    dev = maple_enum_dev(0, 1);

    /* Device was not found and we haven't written that to the screen yet */
    if(!dev && dev_found) {
        clear_screen_info();
        bfont_draw_str(vram_s + INFO_Y * SCREEN_W + 10, SCREEN_W, 0, "No VMU");
        dev_found = false;
    }
    /* Device was found and screen currently says 'No VMU' */
    else if(dev && !dev_found) {
        clear_screen_info();
        draw_dir();
        dev_found = true;
    }

    /* In the other two conditions it's not necessary to update the screen */
}

int wait_start(void) {
    maple_device_t *cont;
    cont_state_t *state;
    bool cont_warning_displayed = false;

    for(;;) {
        cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

        if(!cont) {
            if(!cont_warning_displayed) {
                clear_screen_info();
                bfont_draw_str(vram_s + INFO_Y * SCREEN_W + 10, SCREEN_W, 0, "No Controller");
                cont_warning_displayed = true;
            }
            thd_pass();
            continue;
        }

        state = (cont_state_t *)maple_dev_status(cont);

        if(!state) {
            thd_pass();
            continue;
        }

        new_vmu();

        if(state->buttons & CONT_START)
            return 0;

        thd_pass();
    }
}

static unsigned char vmu_icon[ICON_SIZE * NB_ICONS_MAX];

/* Here's the actual meat of it */
static int write_and_verify_entry(void) {
    uint8_t data[DATA_LEN], verify[DATA_LEN];
    vmu_pkg_t pkg = {0};
    struct stat st;
    ssize_t transferred;
    size_t total;
    int failed = 0;
    file_t f;

    strcpy(pkg.desc_short, "VMU Test");
    strcpy(pkg.desc_long, "This is a test VMU file");
    strcpy(pkg.app_id, "KOS");
    pkg.icon_cnt = NB_ICONS_MAX;
    pkg.icon_data = vmu_icon;
    pkg.icon_anim_speed = 8;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    pkg.data_len = DATA_LEN;
    pkg.data = data;

    for(int i = 0; i < DATA_LEN; i++)
        data[i] = i & 255;

    if(vmu_pkg_load_icon(&pkg, "/rd/ebook.ico") < 0) {
        printf("Unable to load the package icon: %s\n", strerror(errno));
        return -1;
    }

    f = fs_open("/vmu/a1/TESTFILE", O_WRONLY | O_TRUNC);

    if(f == FILEHND_INVALID) {
        printf("Unable to open TESTFILE: %s\n", strerror(errno));
        return -1;
    }

    transferred = fs_write(f, data, sizeof(data));
    if(transferred != (ssize_t)sizeof(data) ||
       fs_vmu_set_header(f, &pkg) < 0)
        failed = 1;
    if(fs_close(f) < 0)
        failed = 1;
    if(failed) {
        printf("Unable to write TESTFILE: %s\n", strerror(errno));
        return -1;
    }

    f = fs_open("/vmu/a1/TESTFILE", O_RDONLY);
    if(f == FILEHND_INVALID) {
        printf("Unable to reopen TESTFILE: %s\n", strerror(errno));
        return -1;
    }

    total = fs_total(f);
    transferred = fs_read(f, verify, sizeof(verify));
    if(total != DATA_LEN || transferred != (ssize_t)sizeof(verify) ||
       memcmp(data, verify, sizeof(data)) != 0)
        failed = 1;
    if(fs_seek(f, -1, SEEK_END) != DATA_LEN - 1 ||
       fs_read(f, verify, 1) != 1 || verify[0] != data[DATA_LEN - 1])
        failed = 1;
    if(fs_close(f) < 0)
        failed = 1;
    if(fs_stat("/vmu/a1/TESTFILE", &st, 0) < 0 ||
       st.st_size != DATA_LEN)
        failed = 1;
    if(failed) {
        printf("TESTFILE payload verification failed: %s\n", strerror(errno));
        return -1;
    }

    f = fs_open("/vmu/a1/TESTFILE", O_RDONLY | O_META);
    if(f == FILEHND_INVALID) {
        printf("Unable to open raw TESTFILE: %s\n", strerror(errno));
        return -1;
    }
    total = fs_total(f);
    if(total <= DATA_LEN || total % VMUFS_BLOCK_SIZE != 0)
        failed = 1;
    if(fs_close(f) < 0)
        failed = 1;
    if(failed) {
        printf("TESTFILE raw allocation verification failed\n");
        return -1;
    }

    printf("Verified %d payload bytes inside %zu stored bytes\n",
           DATA_LEN, total);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    bfont_draw_str(vram_s + 20 * SCREEN_W + 20, SCREEN_W, 0,
                   "Put a VMU you don't care too much about");
    bfont_draw_str(vram_s + 42 * SCREEN_W + 20, SCREEN_W, 0,
                   "in slot A1 and press START");
    bfont_draw_str(vram_s + INFO_Y * SCREEN_W + 10, SCREEN_W, 0, "No VMU");

    if(wait_start() < 0) return 0;

    /* If there was a vmu found, write to it */
    if(dev_found)
        write_and_verify_entry();

    return 0;
}
