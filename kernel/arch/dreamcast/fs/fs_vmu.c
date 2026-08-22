/* KallistiOS ##version##

   fs_vmu.c
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2012, 2013, 2014, 2016 Lawrence Sebald
   Copyright (C) 2026 Joseph Black

*/

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include <kos/mutex.h>
#include <kos/opts.h>
#include <kos/dbglog.h>
#include <dc/fs_vmu.h>
#include <dc/vmufs.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmu_pkg.h>
#include <sys/queue.h>

/*

This is the vmu filesystem module.  Because there are no directories on vmu's
it's pretty simple, however the filesystem uses a separate directory for each
of the vmu slots, so if vmufs were mounted on /vmu, /vmu/a1/ is the dir for
slot 1 on port a, and /vmu/c2 is slot 2 on port c, etc.

Files are cached in full while open because VMU storage is small. Normal opens
of a valid package expose only its logical payload; O_META exposes the complete
block-rounded on-card image. Handles keep payload size, backing capacity, and
package-header offset separate so padding is never reported as file data.

This layer delegates card I/O and mutation ordering to vmufs. An open VFS file
is a private cached snapshot, not a reservation: concurrently modifying the
same file through another handle, vmufs, or direct block access can produce a
lost update even though each individual card transaction remains serialized.

Define VMUFS_DEBUG in kos/opts.h, in your CFLAGS, or here if you want copious
debug output.
*/

#define VMU_DIR     0
#define VMU_FILE    1
#define VMU_ANY     -1  /* Used for checking validity */

/* File handles */
typedef struct vmu_fh_str {
    uint32_t strtype;                   /* 0==dir, 1==file */
    TAILQ_ENTRY(vmu_fh_str) listent;    /* list entry */

    int mode;                           /* mode the file was opened with */
    char path[17];                      /* full path of the file */
    char name[13];                      /* name of the file */
    off_t loc;                          /* current position from the start in the file (bytes) */
    size_t start;                       /* payload offset in the backing buffer */
    maple_device_t *dev;                /* maple address of the vmu to use */
    size_t size;                        /* logical bytes visible through the VFS */
    size_t capacity;                    /* payload bytes available without realloc */
    uint8_t *data;                      /* copy of the whole file */
    vmu_pkg_t *header;                  /* VMU file header */
    bool raw;                           /* file opened as raw */
    bool dirty;                         /* backing data or package metadata changed */
} vmu_fh_t;

/* Directory handles */
typedef struct vmu_dh_str {
    uint32_t strtype;                   /* 0==dir, 1==file */
    TAILQ_ENTRY(vmu_dh_str) listent;    /* list entry */

    int rootdir;                        /* 1 if we're reading /vmu */
    dirent_t dirent;                    /* Dirent to pass back */
    vmu_dir_t *dirblocks;               /* Copy of all directory blocks */
    uint16_t entry;                     /* Current dirent */
    uint16_t dircnt;                    /* Count of dir entries */
    maple_device_t *dev;                /* VMU address */
} vmu_dh_t;

/* Linked list of open files (controlled by "mutex") */
TAILQ_HEAD(vmu_fh_list, vmu_fh_str) vmu_fh;

/* Thread mutex for vmu_fh access */
static mutex_t fh_mutex;

static vmu_pkg_t *dft_header;

static int vmu_pkg_eyecatch_size(int type, size_t *size) {
    switch(type) {
        case VMUPKG_EC_NONE:
            *size = 0;
            return 0;
        case VMUPKG_EC_16BIT:
            *size = 72 * 56 * 2;
            return 0;
        case VMUPKG_EC_256COL:
            *size = 512 + 72 * 56;
            return 0;
        case VMUPKG_EC_16COL:
            *size = 32 + 72 * 56 / 2;
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

static vmu_pkg_t *vmu_pkg_dup(const vmu_pkg_t *old_hdr) {
    size_t ec_size, icon_size;
    vmu_pkg_t *hdr;

    if(!old_hdr || old_hdr->icon_cnt < 0 || old_hdr->icon_cnt > 3 ||
       (old_hdr->icon_cnt && !old_hdr->icon_data) ||
       vmu_pkg_eyecatch_size(old_hdr->eyecatch_type, &ec_size) < 0 ||
       (ec_size && !old_hdr->eyecatch_data)) {
        errno = EINVAL;
        return NULL;
    }

    hdr = malloc(sizeof(*hdr));
    if(!hdr)
        return NULL;

    memcpy(hdr, old_hdr, sizeof(*hdr));

    if(ec_size) {
        hdr->eyecatch_data = malloc(ec_size);
        if(!hdr->eyecatch_data)
            goto err_free_hdr;

        memcpy(hdr->eyecatch_data, old_hdr->eyecatch_data, ec_size);
    } else {
        hdr->eyecatch_data = NULL;
    }

    if(old_hdr->icon_cnt) {
        icon_size = 512 * old_hdr->icon_cnt;

        hdr->icon_data = malloc(icon_size);
        if(!hdr->icon_data)
            goto err_free_ec_data;

        memcpy(hdr->icon_data, old_hdr->icon_data, icon_size);
    } else {
        hdr->icon_data = NULL;
    }

    return hdr;

err_free_ec_data:
    free(hdr->eyecatch_data);
err_free_hdr:
    free(hdr);
    return NULL;
}

/* Take a VMUFS path and return the requested address */
static maple_device_t * vmu_path_to_addr(const char *p) {
    char port;

    if(p[0] != '/') return NULL;            /* Only absolute paths */

    port = p[1] | 32;               /* Lowercase the port */

    if(port < 'a' || port > 'd') return NULL;   /* Unit A-D, device 0-5 */

    if(p[2] < '0' || p[2] > '5') return NULL;

    return maple_enum_dev(port - 'a', p[2] - '0');
}

static int vmu_file_path(const char *path, maple_device_t **dev,
                         const char **name) {
    size_t path_len;
    size_t name_len;

    if(!path || !dev || !name) {
        errno = EINVAL;
        return -1;
    }
    path_len = strlen(path);
    if(path_len < 5u || path[0] != '/') {
        errno = ENOENT;
        return -1;
    }
    *dev = vmu_path_to_addr(path);
    if(!*dev || path[3] != '/' || !path[4] || strchr(path + 4, '/')) {
        errno = ENOENT;
        return -1;
    }
    name_len = strlen(path + 4);
    if(name_len > sizeof(((vmu_fh_t *)0)->name) - 1u) {
        errno = ENAMETOOLONG;
        return -1;
    }
    *name = path + 4;
    return 0;
}

/* Open the fake vmu root dir /vmu */
static vmu_fh_t *vmu_open_vmu_dir(void) {
    unsigned int p, u;
    unsigned int num = 0;
    char names[MAPLE_PORT_COUNT * MAPLE_UNIT_COUNT][2];
    vmu_dh_t *dh;
    maple_device_t * dev;

    /* Determine how many VMUs are connected */
    for(p = 0; p < MAPLE_PORT_COUNT; p++) {
        for(u = 0; u < MAPLE_UNIT_COUNT; u++) {
            dev = maple_enum_dev(p, u);

            if(!dev) continue;

            if(dev->info.functions & MAPLE_FUNC_MEMCARD) {
                names[num][0] = p + 'a';
                names[num][1] = u + '0';
                num++;

                dbglog(DBG_SOURCE(VMUFS_DEBUG), "vmu_open_vmu_dir: found memcard (%c%d)\n",
                       'a' + p, u);
            }
        }
    }

    dbglog(DBG_SOURCE(VMUFS_DEBUG), "# of memcards found: %d\n", num);

    if(!(dh = malloc(sizeof(vmu_dh_t))))
        return NULL;
    memset(dh, 0, sizeof(vmu_dh_t));
    dh->strtype = VMU_DIR;
    dh->dirblocks = num ? malloc(num * sizeof(vmu_dir_t)) : NULL;

    if(num && !dh->dirblocks) {
        free(dh);
        return NULL;
    }

    dh->rootdir = 1;
    dh->entry = 0;
    dh->dircnt = num;
    dh->dev = NULL;

    /* Create the directory entries */
    for(u = 0; u < num; u++) {
        memset(dh->dirblocks + u, 0, sizeof(vmu_dir_t));    /* Start in a clean room */
        memcpy(dh->dirblocks[u].filename, names + u, 2);
        dh->dirblocks[u].filetype = 0xff;
    }

    return (vmu_fh_t *)dh;
}

/* opendir function */
static vmu_fh_t *vmu_open_dir(maple_device_t * dev) {
    vmu_dir_t   * dirents;
    int     dircnt;
    vmu_dh_t    * dh;

    /* Read the VMU's directory */
    if(vmufs_readdir(dev, &dirents, &dircnt) < 0)
        return NULL;

    /* Allocate a handle for the dir blocks */
    if(!(dh = malloc(sizeof(vmu_dh_t)))) {
        free(dirents);
        return NULL;
    }
    dh->strtype = VMU_DIR;
    dh->dirblocks = dirents;
    dh->rootdir = 0;
    dh->entry = 0;
    dh->dircnt = dircnt;
    dh->dev = dev;

    return (vmu_fh_t *)dh;
}

/* openfile function */
static vmu_fh_t *vmu_open_file(maple_device_t * dev, const char *path, int mode) {
    vmu_fh_t *fd;
    int realmode, rv;
    void *data;
    int datasize;
    vmu_pkg_t vmu_pkg;

    if(!(fd = calloc(1, sizeof(*fd))))
        return NULL;

    fd->strtype = VMU_FILE;
    fd->mode = mode;
    strncpy(fd->path, path, 16);
    strncpy(fd->name, path + 4, 12);
    fd->dev = dev;
    fd->raw = !!(mode & O_META);

    /* What mode are we opening in? If we're reading or writing without O_TRUNC
       then we need to read the old file if there is one. */
    realmode = mode & O_MODE_MASK;
    if(realmode != O_RDONLY && realmode != O_WRONLY && realmode != O_RDWR) {
        errno = EINVAL;
        free(fd);
        return NULL;
    }

    if(realmode == O_RDONLY || ((realmode == O_RDWR || realmode == O_WRONLY) && !(mode & O_TRUNC))) {
        /* Try to open it */
        rv = vmufs_read(dev, fd->name, &data, &datasize);

        if(rv < 0) {
            if((realmode == O_RDWR || realmode == O_WRONLY) && rv == -2) {
                /* In some modes failure is ok -- flag to setup a blank first block. */
                datasize = -1;
            }
            else {
                if(rv == -2)
                    errno = ENOENT;
                else if(errno == 0)
                    errno = EIO;
                free(fd);
                return NULL;
            }
        }
    }
    else {
        /* We're writing with truncate... flag to setup a blank first block. */
        datasize = -1;
    }

    /* We were flagged to set up a blank first block */
    if(datasize == -1) {
        data = calloc(1, VMUFS_BLOCK_SIZE);
        if(data == NULL) {
            free(fd);
            return NULL;
        }

        datasize = VMUFS_BLOCK_SIZE;
        fd->capacity = VMUFS_BLOCK_SIZE;
        fd->dirty = true;
    }
    else if(!fd->raw && !vmu_pkg_parse(data, datasize, &vmu_pkg)) {
        fd->header = vmu_pkg_dup(&vmu_pkg);
        if(!fd->header) {
            free(data);
            free(fd);
            return NULL;
        }

        fd->start = (size_t)(vmu_pkg.data - (uint8_t *)data);
        fd->size = (size_t)vmu_pkg.data_len;
        fd->capacity = (size_t)datasize - fd->start;
    }
    else {
        /* Raw opens and unrecognized package files expose the complete stored
           allocation. Package parsing failures are not open failures. */
        fd->size = (size_t)datasize;
        fd->capacity = (size_t)datasize;
    }

    fd->data = (uint8_t *)data;
    if(mode & O_APPEND)
        fd->loc = (off_t)fd->size;

    return fd;
}

/* open function */
static void * vmu_open(vfs_handler_t * vfs, const char *path, int mode) {
    maple_device_t  * dev;      /* maple bus address of the vmu unit */
    vmu_fh_t    *fh;

    (void)vfs;

    if(!*path || (path[0] == '/' && !path[1])) {
        /* /vmu should be opened */
        fh = vmu_open_vmu_dir();
    }
    else {
        /* Figure out which vmu slot is being opened */
        dev = vmu_path_to_addr(path);

        /* printf("VMUFS: card address is %02x\n", addr); */
        if(dev == NULL) return 0;

        /* Check for open as dir */
        if(strlen(path) == 3 || (strlen(path) == 4 && path[3] == '/')) {
            if(!(mode & O_DIR)) return 0;

            fh = vmu_open_dir(dev);
        }
        else {
            size_t name_len;

            if(mode & O_DIR || path[3] != '/' || !path[4] ||
               strchr(path + 4, '/')) {
                errno = ENOENT;
                return NULL;
            }

            name_len = strlen(path + 4);
            if(name_len > 12) {
                errno = ENAMETOOLONG;
                return NULL;
            }

            fh = vmu_open_file(dev, path, mode);
        }
    }

    if(fh == NULL) return 0;

    /* link the fh onto the top of the list */
    mutex_lock(&fh_mutex);
    TAILQ_INSERT_TAIL(&vmu_fh, fh, listent);
    mutex_unlock(&fh_mutex);

    return (void *)fh;
}

/* Verify that a given hnd is actually in the list */
static int vmu_verify_hnd(void * hnd, int type) {
    vmu_fh_t    *cur;
    int     rv;

    rv = 0;

    mutex_lock(&fh_mutex);
    TAILQ_FOREACH(cur, &vmu_fh, listent) {
        if((void *)cur == hnd) {
            rv = 1;
            break;
        }
    }
    mutex_unlock(&fh_mutex);

    if(rv)
        return type == VMU_ANY ? 1 : ((int)cur->strtype == type);
    else
        return 0;
}

/* write a file out before closing it: we aren't perfect on error handling here */
static int vmu_write_close(void * hnd) {
    vmu_fh_t *fh = (vmu_fh_t *)hnd;
    uint8_t *encoded = NULL;
    uint8_t *write_data = fh->data + fh->start;
    const vmu_pkg_t *hdr = fh->raw ? NULL : (fh->header ?: dft_header);
    int ret, write_len;

    if(fh->size > INT_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    write_len = (int)fh->size;

    if(hdr) {
        vmu_pkg_t package = *hdr;

        package.data_len = write_len;
        package.data = write_data;
        ret = vmu_pkg_build(&package, &encoded, &write_len);
        if(ret < 0)
            return ret;
        write_data = encoded;
    }
    else if(!fh->raw)
        dbglog(DBG_WARNING, "VMUFS: file written without header\n");

    ret = vmufs_write(fh->dev, fh->name, write_data, write_len,
                      VMUFS_OVERWRITE);
    free(encoded);

    return ret;
}

/* close a file */
static int vmu_close(void * hnd) {
    vmu_fh_t *fh;
    int st, retval = 0;

    /* Check the handle */
    if(!vmu_verify_hnd(hnd, VMU_ANY)) {
        errno = EBADF;
        return -1;
    }

    fh = (vmu_fh_t *)hnd;

    switch(fh->strtype) {
        case VMU_DIR: {
            vmu_dh_t * dir = (vmu_dh_t *)hnd;

            if(dir->dirblocks)
                free(dir->dirblocks);

            break;
        }

        case VMU_FILE:
            if((fh->mode & O_MODE_MASK) == O_WRONLY ||
                    (fh->mode & O_MODE_MASK) == O_RDWR) {
                if(fh->dirty && (st = vmu_write_close(hnd))) {
                    if(st == -7)
                        errno = ENOSPC;
                    else if(errno == 0)
                        errno = EIO;
                    retval = -1;
                }
            }

            if(fh->header) {
                free(fh->header->eyecatch_data);
                free(fh->header->icon_data);
                free(fh->header);
            }
            free(fh->data);
            break;

    }

    /* Look for the one to get rid of */
    mutex_lock(&fh_mutex);
    TAILQ_REMOVE(&vmu_fh, fh, listent);
    mutex_unlock(&fh_mutex);

    free(fh);
    return retval;
}

/* read function */
static ssize_t vmu_read(void * hnd, void *buffer, size_t cnt) {
    vmu_fh_t *fh;

    /* Check the handle */
    if(!vmu_verify_hnd(hnd, VMU_FILE)) {
        errno = EBADF;
        return -1;
    }

    fh = (vmu_fh_t *)hnd;

    /* make sure we're opened for reading */
    if((fh->mode & O_MODE_MASK) != O_RDONLY &&
       (fh->mode & O_MODE_MASK) != O_RDWR) {
        errno = EBADF;
        return -1;
    }

    if(cnt && !buffer) {
        errno = EFAULT;
        return -1;
    }

    if(cnt == 0)
        return 0;

    if((size_t)fh->loc >= fh->size)
        return 0;

    if(cnt > fh->size - (size_t)fh->loc)
        cnt = fh->size - (size_t)fh->loc;

    memcpy(buffer, fh->data + fh->loc + fh->start, cnt);
    fh->loc += cnt;

    return cnt;
}

/* write function */
static ssize_t vmu_write(void * hnd, const void *buffer, size_t cnt) {
    vmu_fh_t *fh;
    void *tmp;
    size_t end, new_capacity, total_capacity;

    /* Check the handle we were given */
    if(!vmu_verify_hnd(hnd, VMU_FILE)) {
        errno = EBADF;
        return -1;
    }

    fh = (vmu_fh_t *)hnd;

    /* Make sure we're opened for writing */
    if((fh->mode & O_MODE_MASK) != O_WRONLY &&
       (fh->mode & O_MODE_MASK) != O_RDWR) {
        errno = EBADF;
        return -1;
    }

    if(cnt && !buffer) {
        errno = EFAULT;
        return -1;
    }

    if(cnt == 0)
        return 0;

    if(fh->mode & O_APPEND)
        fh->loc = (off_t)fh->size;

    if((uintmax_t)fh->loc > SIZE_MAX ||
       cnt > SIZE_MAX - (size_t)fh->loc) {
        errno = EOVERFLOW;
        return -1;
    }
    end = (size_t)fh->loc + cnt;

    /* Capacity is block-rounded storage; size remains the caller-visible EOF. */
    if(end > fh->capacity) {
        if(end > SIZE_MAX - (VMUFS_BLOCK_SIZE - 1u)) {
            errno = EOVERFLOW;
            return -1;
        }
        new_capacity = (end + VMUFS_BLOCK_SIZE - 1u) &
                       ~(VMUFS_BLOCK_SIZE - 1u);
        if(fh->start > SIZE_MAX - new_capacity) {
            errno = EOVERFLOW;
            return -1;
        }
        total_capacity = fh->start + new_capacity;

        tmp = realloc(fh->data, total_capacity);

        if(!tmp) {
            dbglog(DBG_ERROR, "VMUFS: unable to extend file buffer\n");
            return -1;
        }

        fh->data = tmp;
        memset(fh->data + fh->start + fh->capacity, 0,
               new_capacity - fh->capacity);
        fh->capacity = new_capacity;
    }

    /* A seek beyond EOF creates a deterministic zero-filled gap, even when
       the backing allocation contains old package padding in that interval. */
    if((size_t)fh->loc > fh->size)
        memset(fh->data + fh->start + fh->size, 0,
               (size_t)fh->loc - fh->size);

    memcpy(fh->data + fh->loc + fh->start, buffer, cnt);
    fh->loc += cnt;
    if(end > fh->size)
        fh->size = end;
    if(cnt)
        fh->dirty = true;

    return cnt;
}

/* mmap a file */
/* note: writing past EOF will invalidate your pointer */
static void *vmu_mmap(void * hnd) {
    vmu_fh_t *fh;

    /* Check the handle */
    if(!vmu_verify_hnd(hnd, VMU_FILE))
        return NULL;

    fh = (vmu_fh_t *)hnd;

    if((fh->mode & O_MODE_MASK) == O_WRONLY ||
       (fh->mode & O_MODE_MASK) == O_RDWR)
        fh->dirty = true;

    return fh->data + fh->start;
}

/* Seek elsewhere in a file */
static off_t vmu_seek(void * hnd, off_t offset, int whence) {
    vmu_fh_t *fh;
    off_t base, target;

    /* Check the handle */
    if(!vmu_verify_hnd(hnd, VMU_FILE))
        return -1;

    fh = (vmu_fh_t *)hnd;

    switch(whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = fh->loc;
            break;
        case SEEK_END:
            base = (off_t)fh->size;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if(__builtin_add_overflow(base, offset, &target) || target < 0) {
        errno = EINVAL;
        return -1;
    }

    fh->loc = target;

    return fh->loc;
}

/* tell the current position in the file */
static off_t vmu_tell(void * hnd) {
    /* Check the handle */
    if(!vmu_verify_hnd(hnd, VMU_FILE))
        return -1;

    return ((vmu_fh_t *) hnd)->loc;
}

/* return the filesize */
static size_t vmu_total(void * fd) {
    /* Check the handle */
    if(!vmu_verify_hnd(fd, VMU_FILE))
        return -1;

    return ((vmu_fh_t *)fd)->size;
}

/* read a directory handle */
static const dirent_t *vmu_readdir(void * fd) {
    vmu_dh_t    *dh;
    vmu_dir_t   *dir;

    /* Check the handle */
    if(!vmu_verify_hnd(fd, VMU_DIR)) {
        errno = EBADF;
        return NULL;
    }

    dh = (vmu_dh_t*)fd;

    /* printf("VMUFS: readdir on entry %d of %d\n", dh->entry, dh->dircnt); */

    /* Check if we have any entries left */
    if(dh->entry >= dh->dircnt)
        return NULL;

    /* printf("VMUFS: reading non-null entry %d\n", dh->entry); */

    /* Ok, extract it and fill the dirent struct */
    dir = dh->dirblocks + dh->entry;

    if(dh->rootdir) {
        dh->dirent.size = -1;
        dh->dirent.attr = O_DIR;
    }
    else {
        dh->dirent.size = dir->filesize * 512;
        dh->dirent.attr = 0;
    }

    strncpy(dh->dirent.name, dir->filename, 12);
    dh->dirent.name[12] = 0;
    dh->dirent.time = 0;    /* FIXME */

    /* Move to the next entry */
    dh->entry++;

    return &dh->dirent;
}

static int vmu_ioctl(void *fd, int cmd, va_list ap) {
    vmu_fh_t *fh = (vmu_fh_t*)fd;
    vmu_dh_t *dh = (vmu_dh_t*)fd;
    vmu_pkg_t *old_hdr, *hdr = NULL;
    const vmu_pkg_t *new_hdr;

    if(!dh || (dh->strtype == VMU_DIR && !dh->rootdir)) {
        errno = EBADF;
        return -1;
    }

    switch(cmd) {
    case IOCTL_VMU_SET_HDR:
        new_hdr = va_arg(ap, const vmu_pkg_t *);
        if(new_hdr) {
            hdr = vmu_pkg_dup(new_hdr);
            if(!hdr)
                return -1;
        }

        if(fh->strtype == VMU_FILE) {
            old_hdr = fh->header;
            fh->header = hdr;
            fh->dirty = true;
        } else {
            old_hdr = dft_header;
            dft_header = hdr;
        }

        if(old_hdr) {
            free(old_hdr->icon_data);
            free(old_hdr->eyecatch_data);
            free(old_hdr);
        }
        break;
    }

    return 0;
}

/* Delete a file */
static int vmu_unlink(vfs_handler_t * vfs, const char *path) {
    maple_device_t *dev;
    const char *name;

    (void)vfs;

    if(vmu_file_path(path, &dev, &name) < 0) {
        dbglog(DBG_ERROR, "VMUFS: vmu_unlink on invalid path '%s'\n",
               path ? path : "(null)");
        return -1;
    }

    return vmufs_delete(dev, name);
}

static int vmu_rename(vfs_handler_t *vfs, const char *old_path,
                      const char *new_path) {
    maple_device_t *old_dev, *new_dev;
    const char *old_name, *new_name;

    (void)vfs;

    if(vmu_file_path(old_path, &old_dev, &old_name) < 0 ||
       vmu_file_path(new_path, &new_dev, &new_name) < 0)
        return -1;
    if(old_dev != new_dev) {
        errno = EXDEV;
        return -1;
    }

    return vmufs_rename(old_dev, old_name, new_name);
}

static int vmu_stat(vfs_handler_t *vfs, const char *path, struct stat *st,
                    int flag) {
    maple_device_t *dev;
    size_t len;

    (void)vfs;
    (void)flag;

    if(!path || !st) {
        errno = EINVAL;
        return -1;
    }

    len = strlen(path);

    /* Root directory '/vmu' */
    if(len == 0 || (len == 1 && *path == '/')) {
        memset(st, 0, sizeof(struct stat));
        st->st_dev = (dev_t)('v' | ('m' << 8) | ('u' << 16));
        st->st_mode = S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP | 
            S_IXGRP | S_IROTH | S_IXOTH;
        st->st_size = -1;
        st->st_nlink = 2;

        return 0;
    }

    dev = vmu_path_to_addr(path);

    if(!dev) {
        errno = ENOENT;
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_dev = (dev_t)((uintptr_t)dev);
    st->st_blksize = 512;

    if(len == 3 || (len == 4 && path[3] == '/')) {
        st->st_mode = S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP |
            S_IXGRP | S_IROTH | S_IXOTH;
        st->st_size = vmufs_free_blocks(dev);
        st->st_nlink = 1;
    }
    else if(len > 4 && path[3] == '/' && !strchr(path + 4, '/') &&
            strlen(path + 4) <= sizeof(((vmu_fh_t *)0)->name) - 1u) {
        void *data = NULL;
        int data_size = 0;
        vmu_pkg_t package;
        int saved_errno = errno;

        int rv = vmufs_read(dev, path + 4, &data, &data_size);

        if(rv < 0) {
            if(rv == -2)
                errno = ENOENT;
            else if(errno == 0)
                errno = EIO;
            return -1;
        }

        st->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
        st->st_size = data_size;
        st->st_nlink = 1;
        if(vmu_pkg_parse(data, (size_t)data_size, &package) == 0)
            st->st_size = package.data_len;
        else
            errno = saved_errno;
        free(data);
    }
    else {
        errno = ENOENT;
        return -1;
    }

    return 0;
}

static int vmu_fcntl(void *fd, int cmd, va_list ap) {
    vmu_fh_t *fh;
    int rv = -1;

    /* Check the handle */
    if(!vmu_verify_hnd(fd, VMU_ANY)) {
        errno = EBADF;
        return -1;
    }

    fh = (vmu_fh_t *)fd;

    switch(cmd) {
        case F_GETFL:

            if(fh->strtype)
                rv = fh->mode;
            else
                rv = O_RDONLY | O_DIR;

            break;

        case F_SETFL:
            if(fh->strtype == VMU_FILE) {
                int flags = va_arg(ap, int);

                fh->mode = (fh->mode & ~O_APPEND) | (flags & O_APPEND);
            }
            rv = 0;
            break;

        case F_GETFD:
        case F_SETFD:
            rv = 0;
            break;

        default:
            errno = EINVAL;
    }

    return rv;
}

static int vmu_rewinddir(void * fd) {
    vmu_dh_t *dh;

    /* Check the handle */
    if(!vmu_verify_hnd(fd, VMU_DIR)) {
        errno = EBADF;
        return -1;
    }

    /* Rewind to the beginning of the directory. */
    dh = (vmu_dh_t*)fd;
    dh->entry = 0;

    /* TODO: Technically, we need to re-scan the directory here, but for now we
       will punt on that requirement. */

    return 0;
}

static int vmu_fstat(void *fd, struct stat *st) {
    vmu_fh_t *fh;

    if(!st) {
        errno = EINVAL;
        return -1;
    }

    /* Check the handle */
    if(!vmu_verify_hnd(fd, VMU_ANY)) {
        errno = EBADF;
        return -1;
    }

    fh = (vmu_fh_t *)fd;
    memset(st, 0, sizeof(struct stat));
    st->st_dev = (dev_t)((uintptr_t)fh->dev);
    st->st_mode =  S_IRWXU | S_IRWXG | S_IRWXO;
    st->st_mode |= (fh->strtype == VMU_DIR) ? S_IFDIR : S_IFREG;
    if(fh->strtype == VMU_DIR) {
        vmu_dh_t *dh = (vmu_dh_t *)fh;

        st->st_size = dh->rootdir ? -1 : vmufs_free_blocks(dh->dev);
    }
    else {
        st->st_size = (off_t)fh->size;
    }
    st->st_nlink = (fh->strtype == VMU_DIR) ? 2 : 1;
    st->st_blksize = 512;

    return 0;
}

/* handler interface */
static vfs_handler_t vh = {
    /* Name handler */
    {
        "/vmu",         /* name */
        0,              /* tbfi */
        0x00010000,     /* Version 1.0 */
        0,              /* flags */
        NMMGR_TYPE_VFS, /* VFS handler */
        NMMGR_LIST_INIT
    },
    0, NULL,            /* In-kernel, privdata */

    vmu_open,
    vmu_close,
    vmu_read,
    vmu_write,
    vmu_seek,
    vmu_tell,
    vmu_total,
    vmu_readdir,
    vmu_ioctl,
    vmu_rename,
    vmu_unlink,
    vmu_mmap,
    NULL,               /* complete */
    vmu_stat,
    NULL,               /* mkdir */
    NULL,               /* rmdir */
    vmu_fcntl,
    NULL,               /* poll */
    NULL,               /* link */
    NULL,               /* symlink */
    NULL,               /* seek64 */
    NULL,               /* tell64 */
    NULL,               /* total64 */
    NULL,               /* readlink */
    vmu_rewinddir,
    vmu_fstat
};

int fs_vmu_init(void) {
    TAILQ_INIT(&vmu_fh);
    mutex_init(&fh_mutex, MUTEX_TYPE_NORMAL);
    return nmmgr_handler_add(&vh.nmmgr);
}

int fs_vmu_shutdown(void) {
    vmu_fh_t * c, * n;

    if(nmmgr_handler_remove(&vh.nmmgr) < 0)
        return -1;

    mutex_lock(&fh_mutex);

    TAILQ_FOREACH_SAFE(c, &vmu_fh, listent, n) {

        switch(c->strtype) {
            case VMU_DIR: {
                vmu_dh_t * dir = (vmu_dh_t *)c;
                free(dir->dirblocks);
                break;
            }

            case VMU_FILE:

                if((c->mode & O_MODE_MASK) == O_WRONLY ||
                        (c->mode & O_MODE_MASK) == O_RDWR) {
                    dbglog(DBG_ERROR, "fs_vmu_shutdown: still-open file '%s' not written!\n", c->path);
                }

                if(c->header) {
                    free(c->header->eyecatch_data);
                    free(c->header->icon_data);
                    free(c->header);
                }
                free(c->data);
                break;
        }

        free(c);
    }

    mutex_unlock(&fh_mutex);
    mutex_destroy(&fh_mutex);

    if(dft_header) {
        free(dft_header->eyecatch_data);
        free(dft_header->icon_data);
        free(dft_header);
        dft_header = NULL;
    }

    return 0;
}
