/* KallistiOS ##version##

   fs_iso9660.c
   Copyright (C) 2000, 2001, 2003 Megan Potter
   Copyright (C) 2001 Andrew Kieschnick
   Copyright (C) 2002 Bero
   Copyright (C) 2012, 2013, 2014, 2016 Lawrence Sebald
   Copyright (C) 2025 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

*/

/*

This module implements an ISO9660 file system for reading from a CDR or CD
in the DC's GD-Rom drive.

Rock Ridge support has now been implemented, thanks to Andrew Kieschnick
who donated the code. Thanks to Bero for the Joliet support here.

This FS is considerably simplified from what you'd find in a bigger kernel
like Linux or BSD, since we have the pleasure of working with only a single
device capable of ISO9660 at once =). So there are a number of things in here
that are global variables that might otherwise not be.

Some thanks are in order here to Marcus Comstedt for providing an ISO9660
implementation that was easy enough to understand without downloading the
full spec =). Thanks also in order to the creators of the BSD and Linux
ISO9660 systems, as these were used as references as well.

*/

#include <dc/fs_iso9660.h>
#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>

#include <kos/thread.h>
#include <kos/mutex.h>
#include <kos/fs.h>
#include <kos/opts.h>
#include <kos/dbglog.h>
#include <kos/limits.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdalign.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/queue.h>

#include "../hardware/cdrom_request.h"
#include "../hardware/gdrom_direct_internal.h"
#include <errno.h>
#include <sys/ioctl.h>

static int init_percd(void);
static bool percd_done;
static bool iso_bios_recognition_pending;
static mutex_t backend_mutex;
static fs_iso9660_backend_t iso_backend = FS_ISO9660_BACKEND_BIOS;
static bool iso_backend_locked;
static gdrom_direct_sector_type_t iso_direct_sector_type =
    GDROM_DIRECT_SECTOR_MODE1;
static uint32_t iso_media_generation;
static uint32_t iso_direct_fatal_recovery_generation;
static int iso_media_monitor_ensure(void);

#define ISO_DIRECT_COMMAND_TIMEOUT_MS 4000u
#define ISO_BIOS_RECOGNITION_TIMEOUT_MS 20000u

int fs_iso9660_set_backend(fs_iso9660_backend_t backend) {
    if(backend != FS_ISO9660_BACKEND_BIOS
            && backend != FS_ISO9660_BACKEND_DIRECT) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&backend_mutex);
    if(iso_backend_locked && backend != iso_backend) {
        mutex_unlock(&backend_mutex);
        errno = EBUSY;
        return -1;
    }
    iso_backend = backend;
    mutex_unlock(&backend_mutex);
    cdrom_media_monitor_use_direct(
        backend == FS_ISO9660_BACKEND_DIRECT);
    return 0;
}

fs_iso9660_backend_t fs_iso9660_get_backend(void) {
    fs_iso9660_backend_t backend;

    mutex_lock(&backend_mutex);
    backend = iso_backend;
    mutex_unlock(&backend_mutex);
    return backend;
}

/********************************************************************************/
/* Low-level Joliet utils */

/* Joliet UCS is big endian */
static void utf2ucs(uint8_t *ucs, const uint8_t *utf) {
    int c;

    do {
        c = *utf++;

        if(c <= 0x7f) {
        }
        else if(c < 0xc0) {
            c = (c & 0x1f) << 6;
            c |= (*utf++) & 0x3f;
        }
        else {
            c = (c & 0x0f) << 12;
            c |= ((*utf++) & 0x3f) << 6;
            c |= (*utf++) & 0x3f;
        }

        *ucs++ = c >> 8;
        *ucs++ = c & 0xff;
    }
    while(c);
}

static void ucs2utfn(uint8_t *utf, const uint8_t *ucs, size_t len) {
    int c;

    len = len / 2;

    while(len) {
        len--;
        c = (*ucs++) << 8;
        c |= *ucs++;

        if(c == ';') break;

        if(c <= 0x7f) {
            *utf++ = c;
        }
        else if(c <= 0x7ff) {
            *utf++ = 0xc0 | (c >> 6);
            *utf++ = 0x80 | (c & 0x3f);
        }
        else {
            *utf++ = 0xe0 | (c >> 12);
            *utf++ = 0x80 | ((c >> 6) & 0x3f);
            *utf++ = 0x80 | (c & 0x3f);
        }
    }

    *utf = 0;
}

static int ucscompare(const uint8_t *isofn, const uint8_t *normalfn, int isosize) {
    int i, c0, c1 = 0;

    /* Compare ISO name */
    for(i = 0; i < isosize; i += 2) {
        c0 = ((int)isofn[i] << 8) | ((int)isofn[i + 1]);
        c1 = ((int)normalfn[i] << 8) | ((int)normalfn[i + 1]);

        if(c0 == ';') break;

        /* Otherwise, compare the chars normally */
        if(tolower(c0) != tolower(c1))
            return -1;
    }

    c1 = ((int)normalfn[i] << 8) | (normalfn[i + 1]);

    /* Catch ISO name shorter than normal name */
    if(c1 != '/' && c1 != '\0')
        return -1;
    else
        return 0;
}

static int isjoliet(const char *p) {
    if(p[0] == '%' && p[1] == '/') {
        switch(p[2]) {
            case '@':
                return 1;
            case 'C':
                return 2;
            case 'E':
                return 3;
        }
    }

    return 0;
}

static int joliet;

/********************************************************************************/
/* Low-level ISO utils */

/* ISO Directory entry */
typedef struct {
    uint8_t length;         /* 711 */
    uint8_t ext_attr_length;/* 711 */
    uint8_t extent[8];      /* 733 */
    uint8_t size[8];        /* 733 */
    uint8_t date[7];        /* 7x711 */
    uint8_t flags;
    uint8_t file_unit_size; /* 711 */
    uint8_t interleave;     /* 711 */
    uint8_t vol_sequence[4];/* 723 */
    uint8_t name_len;       /* 711 */
    char    name[1];
} iso_dirent_t;

static inline bool dirent_is_type(const iso_dirent_t *de, bool directory) {
    return !!(de->flags & ISO9660_FILE_DIRECTORY) == directory;
}

/* Util function to reverse the byte order of a uint32_t */
/* static uint32_t ntohl_32(const void *data) {
    const uint8_t *d = (const uint8_t *)data;
    return (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | (d[3] << 0);
} */

/* This seems kinda silly, but it's important since it allows us
   to do unaligned accesses on a buffer */
static uint32_t htohl_32(const void *data) {
    const uint8_t *d = (const uint8_t *)data;
    return (d[0] << 0) | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
}

/* Read red-book section 7.1.1 number (8 bit) */
/* static uint8_t iso_711(const uint8_t *from) { return (*from & 0xff); } */

/* Read red-book section 7.3.3 number (32 bit LE / 32 bit BE) */
static uint32_t iso_733(const uint8_t *from) {
    return htohl_32(from);
}

/* ISO9660 records point at the start of an extent, while file and directory
   payload begins after any extended-attribute blocks. All physical I/O paths
   must use this helper so synchronous, asynchronous, cached, and prefetched
   access agree on the first data sector. */
static int iso_data_extent(uint32_t recorded_extent,
                           uint8_t ext_attr_length,
                           uint32_t *data_extent) {
    uint64_t extent = (uint64_t)recorded_extent + ext_attr_length;

    if(extent > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    *data_extent = (uint32_t)extent;
    return 0;
}


/********************************************************************************/
/* Low-level block caching routines. This implements a simple queue-based
   LRU/MRU caching system. Whenever a block is requested, it will be placed
   on the MRU end of the queue. As more blocks are loaded than can fit in
   the cache, blocks are deleted from the LRU end. */

/* Holds the data for one cache block, and a pointer to the next one.
   As sectors are read from the disc, they are added to the front of
   this cache. As the cache fills up, sectors are removed from the end
   of it. */
typedef struct {
    uint8_t   *data;          /* Sector data */
    uint32_t  sector;         /* CD sector */
} cache_block_t;

/* List of cache blocks (ordered least recently used to most recently) */
#define NUM_CACHE_BLOCKS 16
/* Bound each physical command to 32 KiB, roughly 18 ms at 1.8 MiB/s.
   This is the throughput/responsiveness tuning point and should be adjusted
   only after measurements on real GD-ROM hardware. */
#define ISO_ASYNC_CHUNK_SECTORS 16
#define ISO_ASYNC_BOUNCE_SIZE (ISO_ASYNC_CHUNK_SECTORS * 2048)
static cache_block_t *icache[NUM_CACHE_BLOCKS];     /* inode cache */
static cache_block_t *dcache[NUM_CACHE_BLOCKS];     /* data cache */

static unsigned char *cache_data;
static cache_block_t *caches;
static uint8_t *iso_async_bounce;
static bool iso_initialized;

typedef struct directory_snapshot {
    TAILQ_ENTRY(directory_snapshot) entry;
    uint32_t extent;
    uint32_t size;
    size_t span;
    uint8_t *data;
} directory_snapshot_t;

static TAILQ_HEAD(directory_snapshot_list, directory_snapshot)
    directory_snapshots = TAILQ_HEAD_INITIALIZER(directory_snapshots);
/* Lookups hold this mutex while scanning so eviction cannot free their image.
   The GD request worker also takes it for generation checks and installation.
   Like fh_mutex and cache_mutex, every hold must remain bounded and must never
   include disc I/O or application callbacks: KOS mutexes have no priority
   inheritance. */
static mutex_t directory_snapshot_mutex;
/* Snapshot keys intentionally contain only extent and size. This generation
   is therefore part of their identity: every cache/media reset must clear the
   list and advance it before records from another disc can be consulted. */
static uint32_t directory_snapshot_generation;
static iso9660_cache_stats_t cache_stats;

/* Cache modification mutex */
static mutex_t cache_mutex;

static int iso_direct_error_result(
        int error, const gdrom_direct_result_t *transport) {
    switch(error) {
        case ECANCELED:
            return ERR_ABORTED;
        case ETIMEDOUT:
            return ERR_TIMEOUT;
        case EBUSY:
        case EAGAIN:
            return ERR_BUSY;
        case EINVAL:
            return ERR_ILLEGAL_REQUEST;
        case EIO:
            if(transport && transport->sense_valid)
                return cdrom_sense_to_result(&transport->sense);
            return ERR_SYS;
        default:
            return ERR_SYS;
    }
}

/* Keep synchronous direct reads bounded to the same 32 KiB command size used
   by asynchronous ISO chains. Each direct command releases shared G1
   ownership before the next one, so other GD-ROM or ATA work can run. */
static int iso_backend_read_sectors(void *buffer, uint32_t fad,
                                    size_t sectors) {
    uint8_t *destination = buffer;

    if(iso_backend == FS_ISO9660_BACKEND_BIOS)
        return cdrom_read_sectors_ex(buffer, fad, sectors, true);

    while(sectors) {
        gdrom_direct_result_t transport;
        size_t chunk = sectors > GDROM_DIRECT_DMA_MAX_SECTORS
            ? GDROM_DIRECT_DMA_MAX_SECTORS : sectors;

        if(gdrom_direct_read_sectors_dma(
                destination, fad, chunk, iso_direct_sector_type,
                ISO_DIRECT_COMMAND_TIMEOUT_MS, &transport) < 0)
            return iso_direct_error_result(errno, &transport);

        destination += chunk * GDROM_DIRECT_SECTOR_SIZE;
        fad += chunk;
        sectors -= chunk;
    }

    return ERR_OK;
}

static cdrom_request_t *iso_backend_submit_dma_chain(
    const cdrom_request_dma_segment_t *first, size_t requested_bytes,
    size_t data_bytes, size_t io_bytes, uint32_t timeout,
    cdrom_request_continue_t continuation, void *continuation_data,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data) {
    if(iso_backend == FS_ISO9660_BACKEND_DIRECT)
        return cdrom_request_submit_direct_dma_chain(
            first, iso_direct_sector_type, requested_bytes, data_bytes,
            io_bytes, timeout, continuation, continuation_data, finalizer,
            finalizer_data, callback, callback_data);

    return cdrom_request_submit_dma_chain(
        first, requested_bytes, data_bytes, io_bytes, timeout, continuation,
        continuation_data, finalizer, finalizer_data, callback, callback_data);
}

static int iso_direct_noop_executor(cdrom_request_t *request, void *data) {
    (void)request;
    (void)data;
    return ERR_OK;
}

static cdrom_request_t *iso_backend_submit_noop(
    size_t requested_bytes, cdrom_request_finalizer_t finalizer,
    void *finalizer_data, cdrom_request_callback_t callback,
    void *callback_data) {
    if(iso_backend == FS_ISO9660_BACKEND_DIRECT)
        return cdrom_request_submit_executor(
            CD_CMD_DMAREAD, NULL, 0, requested_bytes, 0, 0, 0,
            iso_direct_noop_executor, finalizer, finalizer_data, callback,
            callback_data);

    return cdrom_request_submit_noop(
        CD_CMD_DMAREAD, requested_bytes, finalizer, finalizer_data, callback,
        callback_data);
}

static void directory_snapshot_clear(void) {
    directory_snapshot_t *snapshot;
    directory_snapshot_t *next;

    mutex_lock(&directory_snapshot_mutex);
    snapshot = TAILQ_FIRST(&directory_snapshots);
    while(snapshot) {
        next = TAILQ_NEXT(snapshot, entry);
        free(snapshot->data);
        free(snapshot);
        snapshot = next;
    }
    TAILQ_INIT(&directory_snapshots);
    if(++directory_snapshot_generation == 0)
        directory_snapshot_generation = 1;
    cache_stats.directory_snapshot_entries = 0;
    cache_stats.directory_snapshot_bytes = 0;
    mutex_unlock(&directory_snapshot_mutex);
}

static void cache_stats_reset(void) {
    mutex_lock(&cache_mutex);
    mutex_lock(&directory_snapshot_mutex);
    memset(&cache_stats, 0, sizeof(cache_stats));
    mutex_unlock(&directory_snapshot_mutex);
    mutex_unlock(&cache_mutex);
}

/* Clears all cache blocks */
static void bclear_cache(cache_block_t **cache) {
    mutex_lock_scoped(&cache_mutex);

    for(size_t i = 0; i < NUM_CACHE_BLOCKS; i++)
        cache[i]->sector = (uint32_t)-1;
}

/* Graduate a block from its current position to the MRU end of the cache */
static void bgrad_cache(cache_block_t **cache, int block) {
    cache_block_t   *tmp;

    /* Don't try it with the end block */
    if(block < 0 || block >= (NUM_CACHE_BLOCKS - 1)) return;

    /* Make a copy and scoot everything down */
    tmp = cache[block];

    for(size_t i = block; i < (NUM_CACHE_BLOCKS - 1); i++)
        cache[i] = cache[i + 1];

    cache[NUM_CACHE_BLOCKS - 1] = tmp;
}

/* Pulls the requested sector into a cache block and returns the cache
   block index. Note that the sector in question may already be in the
   cache, in which case it just returns the containing block. */
static void iso_break_all(void);
static void iso_abort_stream(bool lock);
static int bread_cache(cache_block_t **cache, uint32_t sector) {
    int i, j = ERR_OK, rv;
    bool remount = false;

    rv = -1;
    mutex_lock(&cache_mutex);

    /* Look for a pre-existing cache block */
    for(i = NUM_CACHE_BLOCKS - 1; i >= 0; i--) {
        if(cache[i]->sector == sector) {
            if(cache == icache)
                cache_stats.metadata_sector_hits++;
            bgrad_cache(cache, i);
            rv = NUM_CACHE_BLOCKS - 1;
            goto bread_exit;
        }
    }

    /* If not, look for an open cache slot; if we find one, use it */
    for(i = 0; i < NUM_CACHE_BLOCKS; i++) {
        if(cache[i]->sector == (uint32_t)-1) break;
    }

    /* If we didn't find one, kick an LRU block out of cache */
    if(i >= NUM_CACHE_BLOCKS) {
        i = 0;
        if(cache == icache)
            cache_stats.metadata_sector_evictions++;
    }

    if(cache == icache)
        cache_stats.metadata_sector_misses++;

    iso_abort_stream(cache == icache);
    // dbglog(DBG_DEBUG, "Stream stop for %s read\n", cache == icache ? "cached" : "inode");

    /* Load the requested block */
    j = iso_backend_read_sectors(cache[i]->data, sector + 150, 1);

    if(j != ERR_OK) {
        //dbglog(DBG_ERROR, "fs_iso9660: can't read_sectors for %d: %d\n",
        //  sector+150, j);
        remount = j == ERR_DISC_CHG || j == ERR_NO_DISC;

        rv = -1;
        goto bread_exit;
    }

    cache[i]->sector = sector;

    /* Move it to the most-recently-used position */
    bgrad_cache(cache, i);
    rv = NUM_CACHE_BLOCKS - 1;

    /* Return the new cache block index */
bread_exit:
    mutex_unlock(&cache_mutex);
    /* init_percd() clears both caches and may itself be reading metadata.
       Calling it here used to deadlock on cache_mutex and could recurse on a
       persistent no-disc result. Mark the mount stale and let the next
       foreground entry point perform the normal remount sequence. */
    if(remount) {
        if(j == ERR_DISC_CHG) {
            mutex_lock(&backend_mutex);
            if(iso_backend == FS_ISO9660_BACKEND_BIOS)
                iso_bios_recognition_pending = true;
            mutex_unlock(&backend_mutex);
        }
        percd_done = false;
    }
    return rv;
}

/* read data block */
static inline int bdread(uint32_t sector) {
    return bread_cache(dcache, sector);
}

/* read inode block */
static inline int biread(uint32_t sector) {
    return bread_cache(icache, sector);
}

/* Clear both caches */
static inline void bclear(void) {
    bclear_cache(dcache);
    bclear_cache(icache);
    directory_snapshot_clear();
    cache_stats_reset();
}

/********************************************************************************/
/* Higher-level ISO9660 primitives */

/* Root FS session location (in sectors) */
static uint32_t session_base = 0;

/* Root directory extent and size in bytes */
static uint32_t root_extent = 0, root_size = 0;

/* Root dirent */
static iso_dirent_t root_dirent;


/* Per-disc initialization; this is done every time it's discovered that
   a new CD has been inserted. */
static int init_percd(void) {
    int     i, blk;
    cd_toc_t   toc;
    fs_iso9660_backend_t backend;
    bool recognize_bios_media;
    uint32_t mount_generation;

    /* Do not reserve the media-monitor thread for programs which never touch
       /cd. Once the first mount attempt starts it remains active so later
       eject/insert transitions invalidate this filesystem automatically. */
    (void)iso_media_monitor_ensure();

    dbglog(DBG_NOTICE, "fs_iso9660: disc change detected\n");

    /* Start off with no cached blocks and no open files*/
    iso_reset();

    /* A mount attempt permanently binds this driver instance to one physical
       implementation. This prevents file handles and async chains from
       observing a backend switch underneath them. */
    mutex_lock(&backend_mutex);
    iso_backend_locked = true;
    backend = iso_backend;
    recognize_bios_media = iso_bios_recognition_pending;
    mount_generation = iso_media_generation;
    mutex_unlock(&backend_mutex);

    /* Locate the root session */
    if(backend == FS_ISO9660_BACKEND_DIRECT) {
        gdrom_direct_probe_result_t probe;
        bool recover_fatal = false;

        if(gdrom_direct_probe(&probe, ISO_DIRECT_COMMAND_TIMEOUT_MS) < 0)
            return -1;

        if(probe.status.status == CD_STATUS_FATAL) {
            gdrom_direct_reinit_result_t reinit;

            /* At most one automatic reset is attempted for each observed
               media/drive generation. A permanently fatal drive therefore
               fails subsequent opens promptly instead of entering a reset
               storm; any later significant event grants one fresh attempt. */
            mutex_lock(&backend_mutex);
            if(iso_direct_fatal_recovery_generation
                    != iso_media_generation) {
                iso_direct_fatal_recovery_generation = iso_media_generation;
                recover_fatal = true;
            }
            mutex_unlock(&backend_mutex);

            if(!recover_fatal) {
                errno = EIO;
                return -1;
            }

            dbglog(DBG_WARNING,
                   "fs_iso9660: resetting direct GD-ROM after fatal state\n");
            if(gdrom_direct_reinitialize(
                    &reinit, ISO_DIRECT_COMMAND_TIMEOUT_MS) < 0)
                return -1;
            probe = reinit.probe;
            if(probe.status.status == CD_STATUS_FATAL) {
                errno = EIO;
                return -1;
            }
        }

        if(probe.result != ERR_OK && probe.result != ERR_DISC_CHG) {
            errno = cdrom_result_to_errno(probe.result);
            return -1;
        }

        iso_direct_sector_type =
            probe.status.disc_type == CD_CDROM_XA
                || probe.status.disc_type == CD_CDI
            ? GDROM_DIRECT_SECTOR_MODE2_FORM1
            : GDROM_DIRECT_SECTOR_MODE1;

        if(gdrom_direct_read_toc(
                &toc, probe.status.disc_type == CD_GDROM,
                ISO_DIRECT_COMMAND_TIMEOUT_MS, NULL) < 0)
            return -1;
    }
    else {
        int recognition;
        int disc_type;

        if(recognize_bios_media) {
            /* Media recognition is a BootROM prerequisite to remounting after
               unit attention. A positive result means a
               normal CD/CD-R rather than a Dreamcast dedicated disc; KOS
               supports that low-density medium and must continue mounting it.
               Initial boot media was already recognized by the BootROM, so
               only an observed exchange or unit attention arms this step. */
            recognition = cdrom_media_recognize(
                ISO_BIOS_RECOGNITION_TIMEOUT_MS);
            if(recognition < 0) {
                dbglog(DBG_ERROR,
                       "fs_iso9660:init_percd: media recognition failed: %d\n",
                       errno);
                return -1;
            }

            mutex_lock(&backend_mutex);
            if(mount_generation == iso_media_generation)
                iso_bios_recognition_pending = false;
            mutex_unlock(&backend_mutex);
        }

        if((i = cdrom_reinit()) != 0) {
            dbglog(DBG_ERROR,
                   "fs_iso9660:init_percd: cdrom_reinit returned %d\n", i);
            return -1;
        }

        if((i = cdrom_get_status(NULL, &disc_type)) != ERR_OK) {
            dbglog(DBG_ERROR,
                   "fs_iso9660:init_percd: cdrom_get_status returned %d\n",
                   i);
            return -1;
        }

        if((i = cdrom_read_toc(&toc, disc_type == CD_GDROM)) != 0)
            return i;
    }

    if(!(session_base = cdrom_locate_data_track(&toc)))
        return -1;

    /* Check for joliet extensions */
    joliet = 0;

    for(i = 1; i <= 3; i++) {
        blk = biread(session_base + i + 16 - 150);

        if(blk < 0) return blk;

        if(memcmp((char *)icache[blk]->data, "\02CD001", 6) == 0) {
            joliet = isjoliet((char *)icache[blk]->data + 88);
            dbglog(DBG_NOTICE, "  (joliet level %d extensions detected)\n", joliet);

            if(joliet) break;
        }
    }

    /* If that failed, go after standard/RockRidge ISO */
    if(!joliet) {
        /* Grab and check the volume descriptor */
        blk = biread(session_base + 16 - 150);

        if(blk < 0) return i;

        if(memcmp((char*)icache[blk]->data, "\01CD001", 6)) {
            dbglog(DBG_ERROR, "fs_iso9660: disc is not iso9660\r\n");
            return -1;
        }
    }

    /* Locate the root directory */
    memcpy(&root_dirent, icache[blk]->data + 156, sizeof(iso_dirent_t));
    if(iso_data_extent(iso_733(root_dirent.extent),
                       root_dirent.ext_attr_length, &root_extent) < 0)
        return -1;
    root_size = iso_733(root_dirent.size);

    return 0;
}

/* Compare an ISO9660 filename against a normal filename. This takes into
   account the version code on the end and is not case sensitive. Also
   takes into account the trailing period that some CD burning software
   adds. */
static int fncompare(const char *isofn, int isosize, const char *normalfn) {
    int i;

    /* Compare ISO name */
    for(i = 0; i < isosize; i++) {
        /* Weed out version codes */
        if(isofn[i] == ';') break;

        /* Deal with crap '.' at end of filenames */
        if(isofn[i] == '.' &&
                (i == (isosize - 1) || isofn[i + 1] == ';'))
            break;

        /* Otherwise, compare the chars normally */
        if(tolower((int)isofn[i]) != tolower((int)normalfn[i]))
            return -1;
    }

    /* Catch ISO name shorter than normal name */
    if(normalfn[i] != '/' && normalfn[i] != '\0')
        return -1;
    else
        return 0;
}

/* Locate an ISO9660 object in the given directory; this can be a directory or
   a file, it works fine for either one. Pass in:

   fn:      object filename (relative to the passed directory)
   dir:     0 if looking for a file, 1 if looking for a dir
   dir_extent:  directory extent to start with
   dir_size:    directory size (in bytes)

   It copies the fixed directory-record fields into `result` so cache eviction
   after return cannot invalidate the result.
 */
static bool dirent_matches(const char *fn, int dir, const iso_dirent_t *de,
                           const uint8_t *ucsname) {
    int len;
    const uint8_t *pnt;
    char rrname[NAME_MAX];
    int rrnamelen;

    if(!dirent_is_type(de, dir != 0))
        return false;

    if(joliet)
        return !ucscompare((const uint8_t *)de->name, ucsname,
                           de->name_len);

    rrnamelen = 0;
    len = de->length - sizeof(iso_dirent_t)
          + sizeof(de->name) - de->name_len;
    pnt = (const uint8_t *)de + sizeof(iso_dirent_t)
          - sizeof(de->name) + de->name_len;

    if((de->name_len & 1) == 0) {
        pnt++;
        len--;
    }

    while((len >= 4) && ((pnt[3] == 1) || (pnt[3] == 2))) {
        size_t entry_len = pnt[2];

        if(entry_len < 4 || entry_len > (size_t)len)
            break;
        if(entry_len >= 5 && strncmp((const char *)pnt, "NM", 2) == 0) {
            rrnamelen = entry_len - 5;
            if(rrnamelen >= NAME_MAX)
                rrnamelen = NAME_MAX - 1;
            strncpy(rrname, (const char *)(pnt + 5), rrnamelen);
            rrname[rrnamelen] = 0;
        }

        len -= entry_len;
        pnt += entry_len;
    }

    if(rrnamelen > 0) {
        const char *p = strchr(fn, '/');
        size_t fnlen = p ? (size_t)(p - fn) : strlen(fn);

        return !strncasecmp(rrname, fn, fnlen) && !rrname[fnlen];
    }

    return !fncompare(de->name, de->name_len, fn);
}

static int find_object_in_image(const char *fn, int dir, const uint8_t *image,
                                uint32_t image_size, iso_dirent_t *result) {
    uint8_t ucsname[PATH_MAX * 2 + 2];
    size_t offset = 0;

    if(joliet)
        utf2ucs(ucsname, (const uint8_t *)fn);

    while(offset < image_size) {
        size_t sector_offset = offset & 2047;
        size_t sector_left = 2048 - sector_offset;
        size_t image_left = image_size - offset;
        const iso_dirent_t *de = (const iso_dirent_t *)(image + offset);

        if(!de->length) {
            offset += sector_left;
            continue;
        }

        if(de->length > sector_left || de->length > image_left
                || de->length < sizeof(*de))
            return -1;

        if(dirent_matches(fn, dir, de, ucsname)) {
            memcpy(result, de, sizeof(*result));
            return 0;
        }

        offset += de->length;
    }

    return -1;
}

/* Return true when a complete snapshot existed. In that case `result_status`
   is authoritative even when the requested child was absent. */
static bool find_object_in_snapshot(const char *fn, int dir,
                                    uint32_t extent, uint32_t size,
                                    iso_dirent_t *result,
                                    int *result_status) {
    directory_snapshot_t *snapshot;

    mutex_lock(&directory_snapshot_mutex);
    TAILQ_FOREACH(snapshot, &directory_snapshots, entry) {
        if(snapshot->extent == extent && snapshot->size == size) {
            TAILQ_REMOVE(&directory_snapshots, snapshot, entry);
            TAILQ_INSERT_TAIL(&directory_snapshots, snapshot, entry);
            cache_stats.directory_snapshot_hits++;
            *result_status = find_object_in_image(
                fn, dir, snapshot->data, snapshot->size, result);
            mutex_unlock(&directory_snapshot_mutex);
            return true;
        }
    }
    cache_stats.directory_snapshot_misses++;
    mutex_unlock(&directory_snapshot_mutex);
    return false;
}

static uint32_t directory_snapshot_current_generation(void) {
    uint32_t generation;

    mutex_lock(&directory_snapshot_mutex);
    generation = directory_snapshot_generation;
    mutex_unlock(&directory_snapshot_mutex);
    return generation;
}

/* Takes ownership of `snapshot` and `data` only on success. */
static int directory_snapshot_install(directory_snapshot_t *snapshot,
                                      uint32_t extent, uint32_t size,
                                      size_t span, uint32_t generation,
                                      uint8_t *data) {
    directory_snapshot_t *existing;

    mutex_lock(&directory_snapshot_mutex);
    if(generation != directory_snapshot_generation) {
        mutex_unlock(&directory_snapshot_mutex);
        errno = ESTALE;
        return -1;
    }

    TAILQ_FOREACH(existing, &directory_snapshots, entry) {
        if(existing->extent == extent && existing->size == size) {
            TAILQ_REMOVE(&directory_snapshots, existing, entry);
            cache_stats.directory_snapshot_entries--;
            cache_stats.directory_snapshot_bytes -= existing->span;
            free(existing->data);
            free(existing);
            break;
        }
    }

    while(cache_stats.directory_snapshot_bytes + span
            > ISO9660_DIRECTORY_PREFETCH_BYTES) {
        existing = TAILQ_FIRST(&directory_snapshots);
        if(!existing)
            break;
        TAILQ_REMOVE(&directory_snapshots, existing, entry);
        cache_stats.directory_snapshot_entries--;
        cache_stats.directory_snapshot_bytes -= existing->span;
        cache_stats.directory_snapshot_evictions++;
        free(existing->data);
        free(existing);
    }

    snapshot->extent = extent;
    snapshot->size = size;
    snapshot->span = span;
    snapshot->data = data;
    TAILQ_INSERT_TAIL(&directory_snapshots, snapshot, entry);
    cache_stats.directory_snapshot_entries++;
    cache_stats.directory_snapshot_bytes += span;
    mutex_unlock(&directory_snapshot_mutex);
    return 0;
}

static int find_object(const char *fn, int dir, uint32_t dir_extent,
                       uint32_t dir_size, iso_dirent_t *result) {
    int     i, c;
    int snapshot_result;
    iso_dirent_t    *de;
    int     size_left;

    /* We need this to be signed for our while loop to end properly */
    size_left = (int)dir_size;

    /* Joliet */
    uint8_t ucsname[PATH_MAX * 2 + 2];

    if(find_object_in_snapshot(fn, dir, dir_extent, dir_size, result,
                               &snapshot_result))
        return snapshot_result;

    /* If this is a Joliet CD, then UCSify the name */
    if(joliet)
        utf2ucs(ucsname, (uint8_t *)fn);

    while(size_left > 0) {
        c = biread(dir_extent);

        if(c < 0) return -1;

        for(i = 0; i < 2048 && i < size_left;) {
            /* Locate the current dirent */
            de = (iso_dirent_t *)(icache[c]->data + i);

            if(!de->length) break;

            if(dirent_matches(fn, dir, de, ucsname)) {
                memcpy(result, de, sizeof(*result));
                return 0;
            }

            i += de->length;
        }

        dir_extent++;
        size_left -= 2048;
    }

    return -1;
}

/* Locate an ISO9660 object anywhere on the disc, starting at the root,
   and expecting a fully qualified path name. This is analogous to find_object
   but it searches with the path in mind.

   fn:      object filename (relative to the passed directory)
   dir:     0 if looking for a file, 1 if looking for a dir
   dir_extent:  directory extent to start with
   dir_size:    directory size (in bytes)

   It copies the fixed directory-record fields into `result`.
 */
static int find_object_path(const char *fn, int dir,
                            const iso_dirent_t *start,
                            iso_dirent_t *result) {
    const char *cur;
    iso_dirent_t current = *start;
    iso_dirent_t next;
    uint32_t data_extent;

    /* If the object is in a sub-tree, traverse the trees looking
       for the right directory */
    while((cur = strchr(fn, '/'))) {
        if(cur != fn) {
            /* Note: trailing path parts don't matter since find_object
               only compares based on the FN length on the disc. */
            if(iso_data_extent(iso_733(current.extent),
                               current.ext_attr_length, &data_extent) < 0
                    || find_object(fn, 1, data_extent,
                           iso_733(current.size), &next) < 0)
                return -1;
            current = next;
        }

        fn = cur + 1;
    }

    /* Locate the file in the resulting directory */
    if(*fn) {
        if(iso_data_extent(iso_733(current.extent),
                           current.ext_attr_length, &data_extent) < 0)
            return -1;
        return find_object(fn, dir, data_extent, iso_733(current.size),
                           result);
    }

    if(!dir)
        return -1;

    *result = current;
    return 0;
}

/********************************************************************************/
/* File primitives */

typedef struct iso_fd {
    TAILQ_ENTRY(iso_fd) next;   /* Next handle in the linked list */
    uint32_t first_extent;      /* First sector */
    bool dir;                   /* True if a directory */
    uint32_t ptr;               /* Current read position in bytes */
    uint32_t size;              /* Length of file in bytes */
    uint8_t flags;              /* ISO9660 directory record flags */
    uint8_t ext_attr_length;    /* Extended attribute length in sectors */
    uint8_t file_unit_size;     /* Interleaved file unit size */
    uint8_t interleave_gap;     /* Interleaved gap size */
    dirent_t dirent;            /* A static dirent to pass back to clients */
    bool broken;                /* True if the CD has been swapped out since open */
    cdrom_request_t *async_request; /* Active sector request, if any */
    cdrom_stream_session_t *stream_session; /* Active staged stream, if any */
    size_t stream_part;         /* Stream DMA part of 32 bytes */
    uint8_t alignas(32) stream_data[32];
} iso_fd_t;

static TAILQ_HEAD(iso_fd_queue, iso_fd) iso_fd_queue;

/* Mutex for protecting access to the iso_fd_queue. Async submission holds
   this through both request queueing and fd->async_request publication. A
   fast finalizer takes the same lock, so narrowing that region would permit
   it to clear the slot before the submitter publishes the request. */
static mutex_t fh_mutex;
static iso_fd_t *stream_fd = NULL;
static vfs_handler_t vh;

static inline bool iso_fd_async_busy(const iso_fd_t *fd) {
    return fd->async_request || fd->stream_session;
}

typedef struct iso_async_read {
    iso_fd_t *fd;
    file_t retained_fd;
    uint8_t *buffer;
    uint32_t start;
    uint32_t base_fad;
    size_t data_size;
    size_t transfer_size;
    size_t data_delivered;
    size_t io_delivered;
    cdrom_request_callback_t callback;
    void *callback_data;
} iso_async_read_t;

typedef struct iso_async_byte_read {
    iso_fd_t *fd;
    file_t retained_fd;
    uint8_t *buffer;
    uint32_t start;
    uint32_t base_fad;
    size_t data_size;
    size_t delivered;
    cdrom_request_callback_t callback;
    void *callback_data;
} iso_async_byte_read_t;

static bool iso_async_byte_needs_bounce(
    const iso_async_byte_read_t *async) {
    return async->data_size &&
           ((async->start & 2047) || ((uintptr_t)async->buffer & 31) ||
            (async->data_size & 2047));
}

/* The caller holds fh_mutex. This makes first-use allocation atomic with
   request publication and keeps the shared workspace alive until request
   teardown has completed before fs_iso9660_shutdown(). */
static int iso_async_bounce_ensure(void) {
    if(iso_async_bounce)
        return 0;

    iso_async_bounce = aligned_alloc(32, ISO_ASYNC_BOUNCE_SIZE);
    if(!iso_async_bounce) {
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

typedef struct iso_async_preseek {
    iso_fd_t *fd;
    file_t retained_fd;
    cdrom_request_callback_t callback;
    void *callback_data;
} iso_async_preseek_t;

typedef struct iso_async_stream {
    iso_fd_t *fd;
    file_t retained_fd;
    uint32_t start;
} iso_async_stream_t;

/* Break all of our open file descriptor. This is necessary when the disc
   is changed so that we don't accidentally try to keep on doing stuff
   with the old info. As files are closed and re-opened, the broken flag
   will be cleared. */
static inline void iso_break_all(void) {
    iso_fd_t *fd;

    mutex_lock_scoped(&fh_mutex);

    TAILQ_FOREACH(fd, &iso_fd_queue, next) {
        fd->broken = true;
    }
}

/* Abort the current stream. */
static inline void iso_abort_stream(bool lock) {
    if(stream_fd) {
        if(lock)
            mutex_lock(&fh_mutex);

        cdrom_stream_stop(false);
        stream_fd->stream_part = 0;
        stream_fd = NULL;

        if(lock)
            mutex_unlock(&fh_mutex);
    }
}

/* Open a file or directory */
static void * iso_open(vfs_handler_t * vfs, const char *fn, int mode) {
    iso_dirent_t de;
    iso_fd_t *fd;

    (void)vfs;

    /* Make sure they don't want to open things as writeable */
    if((mode & O_MODE_MASK) != O_RDONLY) {
        errno = EROFS;
        return 0;
    }

    /* Do this only when we need to (this is still imperfect) */
    if(!percd_done && init_percd() < 0) {
        errno = ENODEV;
        return 0;
    }

    percd_done = true;

    /* Find the file we want */
    if(find_object_path(fn, (mode & O_DIR) ? 1 : 0,
                        &root_dirent, &de) < 0) {
        errno = ENOENT;
        return 0;
    }

    fd = aligned_alloc(32, sizeof(*fd));
    if(!fd) {
        errno = ENOMEM;
        return 0;
    }

    /* Fill in the file handle and return the fd */
    *fd = (iso_fd_t){
        .first_extent = iso_733(de.extent),
        .dir = (mode & O_DIR) != 0,
        .size = iso_733(de.size),
        .flags = de.flags,
        .ext_attr_length = de.ext_attr_length,
        .file_unit_size = de.file_unit_size,
        .interleave_gap = de.interleave,
        .broken = false,
        .stream_part = 0,
        .stream_data = {0},
    };

    mutex_lock_scoped(&fh_mutex);

    TAILQ_INSERT_TAIL(&iso_fd_queue, fd, next);

    return fd;
}

/* Close a file or directory */
static int iso_close(void * h) {
    iso_fd_t *fd = (iso_fd_t *)h;

    mutex_lock_scoped(&fh_mutex);

    if(fd == stream_fd) {
        iso_abort_stream(false);
        // dbglog(DBG_DEBUG, "Stream stop on close, fd=%p\n", fd);
    }

    TAILQ_REMOVE(&iso_fd_queue, fd, next);
    free(fd);

    return 0;
}

static int iso_stream_done(size_t *remain_size) {
    return cdrom_stream_progress(remain_size) != 1;
}

/* Read from a file */
static ssize_t iso_read(void *h, void *buf, size_t bytes) {
    int rv, c;
    size_t toread, thissect;
    uint8_t *outbuf;
    size_t remain_size = 0, req_size;
    uint32_t data_extent, sector;
    iso_fd_t *fd = (iso_fd_t *)h;

    /* Check that the fd is valid */
    if(fd->first_extent == 0 || fd->broken) {
        errno = EBADF;
        return -1;
    }
    if(iso_data_extent(fd->first_extent, fd->ext_attr_length,
                       &data_extent) < 0
            || (fd->size && (uint64_t)data_extent
                + (fd->size - 1) / 2048 > UINT32_MAX)) {
        errno = EOVERFLOW;
        return -1;
    }

    rv = 0;
    outbuf = (uint8_t *)buf;
    mutex_lock(&fh_mutex);

    if(iso_fd_async_busy(fd)) {
        errno = EBUSY;
        mutex_unlock(&fh_mutex);
        return -1;
    }

    /* Read zero or more sectors into the buffer from the current pos */
    while(bytes > 0) {
        /* Figure out how much we still need to read */
        toread = (bytes > (fd->size - fd->ptr)) ? fd->size - fd->ptr : bytes;

        if(toread == 0) break;

        /* If we have partial data from a stream, use it */
        if(fd->stream_part > 0) {
            size_t avail = 32 - fd->stream_part;
            size_t given = (toread > avail) ? avail : toread;

            memcpy(outbuf, &fd->stream_data[fd->stream_part], given);
            fd->stream_part = (fd->stream_part + given) & 31;

            outbuf += given;
            fd->ptr += given;
            bytes -= given;
            rv += given;
            toread -= given;

            if(toread == 0) continue;
        }

        /* How much more can we read in the current sector? */
        thissect = 2048 - (fd->ptr % 2048);
        sector = data_extent + (fd->ptr / 2048);

        if(iso_backend == FS_ISO9660_BACKEND_BIOS
                && (thissect & 31) == 0 && toread >= 32
                && (((uintptr_t)outbuf) & 31) == 0) {

            if(stream_fd == fd) {
                toread &= ~31;
                c = cdrom_stream_request(outbuf, toread, 1);

                if(c) {
                    goto read_error;
                }
                cdrom_stream_progress(&remain_size);
                // dbglog(DBG_DEBUG, "Stream request: read=%d remain=%d out=%p fd=%p\n",
                //         toread, remain_size, outbuf, fd);
            }
            else if(thissect == 2048) {
                req_size = (fd->size - fd->ptr);

                if(req_size & 2047) {
                    req_size = (req_size + 2048) & ~2047;
                }
                if(stream_fd) {
                    iso_abort_stream(false);
                    // dbglog(DBG_DEBUG, "Stream stop for file fd: %p -> %p\n", stream_fd, fd);
                }
                c = cdrom_stream_start(sector + 150, req_size / 2048, true);

                if(c) {
                    goto read_loop;
                }
                fd->stream_part = 0;
                stream_fd = fd;
                // dbglog(DBG_DEBUG, "Stream start: lba=%ld cnt=%d fd=%p\n",
                //     sector + 150, req_size / 2048, fd);

                toread &= ~31;
                c = cdrom_stream_request(outbuf, toread, 1);

                if(c) {
                    goto read_error;
                }
                cdrom_stream_progress(&remain_size);
                // dbglog(DBG_DEBUG, "Stream request: read=%d remain=%d out=%p fd=%p\n",
                //         toread, remain_size, outbuf, fd);
            }
            else {
                goto read_loop;
            }

            if(remain_size == 0) {
                iso_abort_stream(false);
                // dbglog(DBG_DEBUG, "Stream stop on end, fd=%p\n", fd);
            }
            goto end_loop;
        }
        else if(iso_backend == FS_ISO9660_BACKEND_BIOS
                && stream_fd == fd && toread < 32) {

            toread = (toread > thissect) ? thissect : toread;

            c = cdrom_stream_request(fd->stream_data, 32, 0);
            if(c) {
                goto read_error;
            }

            thd_poll((thd_cb_t)iso_stream_done, &remain_size, 0);

            memcpy(outbuf, fd->stream_data, toread);
            fd->stream_part = toread & 31;

            // dbglog(DBG_DEBUG, "Stream request: read=%d remain=%d part=%d out=%p fd=%p\n",
            //         toread, remain_size, fd->stream_part, outbuf, fd);

            if(remain_size == 0) {
                iso_abort_stream(false);
                // dbglog(DBG_DEBUG, "Stream stop on end, fd=%p\n", fd);
            }
            goto end_loop;
        }

read_loop:
        /* If we're on a sector boundary and we have more than one
           full sector to read, then short-circuit the cache here
           and use the multi-sector reads from the CD unit. */
        if(thissect == 2048 && toread >= 2048 && __is_aligned(outbuf, 32)) {
            /* Round it off to an even sector count. */
            thissect = toread / 2048;
            toread = thissect * 2048;
            c = iso_backend_read_sectors(outbuf, sector + 150, thissect);

            if(c) {
                goto read_error;
            }
        }
        else {
            toread = (toread > thissect) ? thissect : toread;
            c = bdread(sector);

            if(c < 0) {
                goto read_error;
            }
            memcpy(outbuf, dcache[c]->data + (fd->ptr % 2048), toread);
        }

end_loop:
        /* Adjust pointers */
        outbuf += toread;
        fd->ptr += toread;
        bytes -= toread;
        rv += toread;
    }

    mutex_unlock(&fh_mutex);
    return rv;

read_error:
    errno = cdrom_result_to_errno(c);
    mutex_unlock(&fh_mutex);
    return -1;
}

/* Seek elsewhere in a file */
static off_t iso_seek(void * h, off_t offset, int whence) {
    uint32_t old_ptr;
    iso_fd_t *fd = (iso_fd_t *)h;

    /* Check that the fd is valid */
    if(fd->first_extent == 0 || fd->broken) {
        errno = EBADF;
        return -1;
    }

    mutex_lock_scoped(&fh_mutex);

    if(iso_fd_async_busy(fd)) {
        errno = EBUSY;
        return -1;
    }

    old_ptr = fd->ptr;

    /* Update current position according to arguments */
    switch(whence) {
        case SEEK_SET:
            if(offset < 0) {
                errno = EINVAL;
                return -1;
            }

            fd->ptr = offset;
            break;

        case SEEK_CUR:
            if(offset < 0 && ((uint32_t)-offset) > fd->ptr) {
                errno = EINVAL;
                return -1;
            }

            fd->ptr += offset;
            break;

        case SEEK_END:
            if(offset < 0 && ((uint32_t)-offset) > fd->size) {
                errno = EINVAL;
                return -1;
            }

            fd->ptr = fd->size + offset;
            break;

        default:
            errno = EINVAL;
            return -1;
    }

    /* Check bounds */
    if(fd->ptr > fd->size) fd->ptr = fd->size;

    if(fd == stream_fd && old_ptr != fd->ptr) {
        iso_abort_stream(false);
        // dbglog(DBG_DEBUG, "Stream stop on seek: %ld != %ld\n", old_ptr, fd->ptr);
    }

    return fd->ptr;
}

/* Tell where in the file we are */
static off_t iso_tell(void * h) {
    iso_fd_t *fd = (iso_fd_t *)h;

    if(fd->first_extent == 0 || fd->broken) {
        errno = EBADF;
        return -1;
    }

    return fd->ptr;
}

/* Tell how big the file is */
static size_t iso_total(void * h) {
    iso_fd_t *fd = (iso_fd_t *)h;

    if(fd->first_extent == 0 || fd->broken) {
        errno = EBADF;
        return -1;
    }

    return fd->size;
}

/* Helper function for readdir: post-processes an ISO filename to make
   it a bit prettier. */
static void fn_postprocess(char *fnin) {
    char    * fn = fnin;

    while(*fn && *fn != ';') {
        *fn = tolower((int) * fn);
        fn++;
    }

    *fn = 0;

    /* Strip trailing dots */
    if(fn > fnin && fn[-1] == '.') {
        fn[-1] = 0;
    }
}

/* Read a directory entry */
static const dirent_t *iso_readdir(void * h) {
    int     c;
    iso_dirent_t    *de;
    uint32_t data_extent;

    /* RockRidge */
    int     len;
    uint8_t       *pnt;

    iso_fd_t *fd = (iso_fd_t *)h;

    if(fd->first_extent == 0 || !fd->dir || fd->broken) {
        errno = EBADF;
        return NULL;
    }
    if(iso_data_extent(fd->first_extent, fd->ext_attr_length,
                       &data_extent) < 0
            || (fd->size && (uint64_t)data_extent
                + (fd->size - 1) / 2048 > UINT32_MAX)) {
        errno = EOVERFLOW;
        return NULL;
    }

    /* Scan forwards until we find the next valid entry, an
       end-of-entry mark, or run out of dir size. */
    c = -1;
    de = NULL;

    while(fd->ptr < fd->size) {
        /* Get the current dirent block */
        c = biread(data_extent + fd->ptr / 2048);

        if(c < 0) return NULL;

        de = (iso_dirent_t *)(icache[c]->data + (fd->ptr % 2048));

        if(de->length) break;

        /* Skip to the next sector */
        fd->ptr += 2048 - (fd->ptr % 2048);
    }

    if(fd->ptr >= fd->size) return NULL;

    /* If we're at the first, skip the two blank entries */
    if(!de->name[0] && de->name_len == 1) {
        fd->ptr += de->length;
        de = (iso_dirent_t *)(icache[c]->data + (fd->ptr % 2048));
        fd->ptr += de->length;
        de = (iso_dirent_t *)(icache[c]->data + (fd->ptr % 2048));

        if(!de->length) return NULL;
    }

    if(joliet) {
        ucs2utfn((uint8_t *)fd->dirent.name, (uint8_t *)de->name, de->name_len);
    }
    else {
        /* Fill out the VFS dirent */
        strncpy(fd->dirent.name, de->name, de->name_len);
        fd->dirent.name[de->name_len] = 0;
        fn_postprocess(fd->dirent.name);

        /* Check for Rock Ridge NM extension */
        len = de->length - sizeof(iso_dirent_t) + sizeof(de->name) - de->name_len;
        pnt = (uint8_t *)de + sizeof(iso_dirent_t) - sizeof(de->name) + de->name_len;

        if((de->name_len & 1) == 0) {
            pnt++;
            len--;
        }

        while((len >= 4) && ((pnt[3] == 1) || (pnt[3] == 2))) {
            if(strncmp((char *)pnt, "NM", 2) == 0) {
                strncpy(fd->dirent.name, (char *)(pnt + 5), pnt[2] - 5);
                fd->dirent.name[pnt[2] - 5] = 0;
            }

            len -= pnt[2];
            pnt += pnt[2];
        }
    }

    if(de->flags & ISO9660_FILE_DIRECTORY) {
        fd->dirent.size = -1;
        fd->dirent.attr = O_DIR;
    }
    else {
        fd->dirent.size = iso_733(de->size);
        fd->dirent.attr = 0;
    }

    fd->ptr += de->length;

    return &fd->dirent;
}

static int iso_ioctl(void *h, int cmd, va_list ap) {
    iso_fd_t *fd = (iso_fd_t *)h;
    void *arg = va_arg(ap, void*);

    switch(cmd) {
        case IOCTL_ISO9660_GET_FILE_INFO: {
            iso9660_file_info_t *info = arg;

            if(!info) {
                errno = EINVAL;
                return -1;
            }

            if(!fd->first_extent || fd->broken) {
                errno = EBADF;
                return -1;
            }

            *info = (iso9660_file_info_t) {
                .extent_lba = fd->first_extent,
                .extent_fad = fd->first_extent + 150,
                .size = fd->size,
                .sector_count = fd->size / 2048 + !!(fd->size % 2048),
                .flags = fd->flags,
                .ext_attr_length = fd->ext_attr_length,
                .file_unit_size = fd->file_unit_size,
                .interleave_gap = fd->interleave_gap,
            };
            return 0;
        }
        case IOCTL_FS_ROOTBUS_DMA_READY:
            if(arg != NULL) {
                *(uint32_t *)arg = 32;
            }
            if(stream_fd == fd) {
                return (fd->ptr & 31) ? -1 : 0;
            }
            return (fd->ptr & 2047) ? -1 : 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int fs_iso9660_get_file_info(file_t fd, iso9660_file_info_t *info) {
    if(!info) {
        errno = EINVAL;
        return -1;
    }

    return fs_ioctl(fd, IOCTL_ISO9660_GET_FILE_INFO, info);
}

int fs_iso9660_get_path_info(const char *path, iso9660_file_info_t *info) {
    file_t fd;
    int rv;

    if(!path || !info) {
        errno = EINVAL;
        return -1;
    }

    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID && errno == ENOENT) {
        fd = fs_open(path, O_RDONLY | O_DIR);
    }

    if(fd == FILEHND_INVALID)
        return -1;

    rv = fs_iso9660_get_file_info(fd, info);
    if(rv < 0) {
        int saved_errno = errno;
        fs_close(fd);
        errno = saved_errno;
    }
    else {
        fs_close(fd);
    }
    return rv;
}

int fs_iso9660_get_cache_stats(iso9660_cache_stats_t *stats) {
    if(!stats) {
        errno = EINVAL;
        return -1;
    }

    mutex_lock(&cache_mutex);
    mutex_lock(&directory_snapshot_mutex);
    *stats = cache_stats;
    mutex_unlock(&directory_snapshot_mutex);
    mutex_unlock(&cache_mutex);
    return 0;
}

typedef struct iso_async_directory_prefetch {
    iso_fd_t *fd;
    file_t retained_fd;
    directory_snapshot_t *snapshot;
    uint8_t *buffer;
    uint32_t extent;
    uint32_t size;
    uint32_t generation;
    size_t span;
    size_t delivered;
    cdrom_request_callback_t callback;
    void *callback_data;
} iso_async_directory_prefetch_t;

static int iso_directory_prefetch_make_segment(
    iso_async_directory_prefetch_t *async,
    cdrom_request_dma_segment_t *segment) {
    size_t remaining = async->span - async->delivered;
    size_t sectors = remaining / 2048;
    uint64_t fad = (uint64_t)async->extent
        + async->delivered / 2048 + 150;

    if(!remaining || remaining & 2047) {
        errno = EINVAL;
        return -1;
    }
    if(sectors > ISO_ASYNC_CHUNK_SECTORS)
        sectors = ISO_ASYNC_CHUNK_SECTORS;

    /* This DMA populates driver-owned metadata. Keep caller-visible payload
       accounting at zero and expose progress only through the physical-I/O
       counters. */
    if(fad > UINT32_MAX
            || cdrom_request_dma_segment_init(
                segment, async->buffer + async->delivered, (uint32_t)fad,
                sectors, 0, 0, false) < 0)
        return -1;
    if(segment->io_bytes != sectors * 2048) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int iso_directory_prefetch_continue(
    cdrom_request_t *request,
    const cdrom_request_dma_segment_t *completed,
    cdrom_request_dma_segment_t *next, void *data) {
    iso_async_directory_prefetch_t *async = data;

    (void)request;

    async->delivered += completed->io_bytes;
    if(async->generation != directory_snapshot_current_generation())
        return -1;
    if(async->delivered == async->span)
        return 0;
    return iso_directory_prefetch_make_segment(async, next) < 0 ? -1 : 1;
}

static void iso_directory_prefetch_release(
    iso_async_directory_prefetch_t *async) {
    free(async->snapshot);
    free(async->buffer);
    fs_close(async->retained_fd);
    free(async);
}

static void iso_directory_prefetch_finalize(
    cdrom_request_t *request, const cdrom_request_status_t *status,
    void *data) {
    iso_async_directory_prefetch_t *async = data;
    bool install;

    mutex_lock(&fh_mutex);
    install = status->state == CDROM_REQUEST_COMPLETE
        && !async->fd->broken && async->snapshot && async->buffer;
    if(async->fd->async_request == request)
        async->fd->async_request = NULL;
    mutex_unlock(&fh_mutex);

    if(install && directory_snapshot_install(
            async->snapshot, async->extent, async->size, async->span,
            async->generation, async->buffer) == 0) {
        async->snapshot = NULL;
        async->buffer = NULL;
    }

    if(!async->callback)
        iso_directory_prefetch_release(async);
}

static void iso_directory_prefetch_complete(
    cdrom_request_t *request, const cdrom_request_status_t *status,
    void *data) {
    iso_async_directory_prefetch_t *async = data;

    if(async->callback)
        async->callback(request, status, async->callback_data);
    iso_directory_prefetch_release(async);
}

cdrom_request_t *fs_iso9660_prefetch_directory_async(
    file_t directory, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data) {
    iso_async_directory_prefetch_t *async = NULL;
    cdrom_request_dma_segment_t first;
    cdrom_request_t *request;
    iso_fd_t *fd;
    file_t retained = FILEHND_INVALID;
    uint64_t span;
    uint64_t last_fad;
    uint32_t data_extent;
    int saved_errno;

    retained = fs_dup(directory);
    if(retained == FILEHND_INVALID)
        return NULL;
    if(fs_get_handler(retained) != &vh
            || !(fd = (iso_fd_t *)fs_get_handle(retained))) {
        errno = EXDEV;
        goto fail;
    }

    async = calloc(1, sizeof(*async));
    if(!async) {
        errno = ENOMEM;
        goto fail;
    }

    mutex_lock(&fh_mutex);
    if(!fd->first_extent || fd->broken) {
        errno = EBADF;
        goto fail_locked;
    }
    if(!fd->dir) {
        errno = ENOTDIR;
        goto fail_locked;
    }
    if(iso_fd_async_busy(fd)) {
        errno = EBUSY;
        goto fail_locked;
    }
    if(fd->flags & ISO9660_FILE_MULTI_EXTENT
            || fd->file_unit_size || fd->interleave_gap) {
        errno = ENOTSUP;
        goto fail_locked;
    }

    span = ((uint64_t)fd->size + 2047) & ~(uint64_t)2047;
    if(!span || span > ISO9660_DIRECTORY_PREFETCH_BYTES
            || span > SIZE_MAX) {
        errno = span ? EFBIG : EINVAL;
        goto fail_locked;
    }
    if(iso_data_extent(fd->first_extent, fd->ext_attr_length,
                       &data_extent) < 0)
        goto fail_locked;
    last_fad = (uint64_t)data_extent + span / 2048 - 1 + 150;
    if(last_fad > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail_locked;
    }

    async->fd = fd;
    async->retained_fd = retained;
    async->extent = data_extent;
    async->size = fd->size;
    async->span = (size_t)span;
    async->generation = directory_snapshot_current_generation();
    async->callback = callback;
    async->callback_data = callback_data;

    async->snapshot = malloc(sizeof(*async->snapshot));
    async->buffer = aligned_alloc(32, async->span);
    if(!async->snapshot || !async->buffer) {
        errno = ENOMEM;
        goto fail_locked;
    }
    if(iso_directory_prefetch_make_segment(async, &first) < 0)
        goto fail_locked;

    if(stream_fd)
        iso_abort_stream(false);
    request = iso_backend_submit_dma_chain(
        &first, 0, 0, async->span, timeout,
        iso_directory_prefetch_continue, async,
        iso_directory_prefetch_finalize, async,
        callback ? iso_directory_prefetch_complete : NULL, async);

    if(!request)
        goto fail_locked;
    fd->async_request = request;
    mutex_unlock(&fh_mutex);
    return request;

fail_locked:
    saved_errno = errno;
    mutex_unlock(&fh_mutex);
    free(async ? async->snapshot : NULL);
    free(async ? async->buffer : NULL);
    free(async);
    fs_close(retained);
    errno = saved_errno;
    return NULL;

fail:
    saved_errno = errno;
    free(async);
    if(retained != FILEHND_INVALID)
        fs_close(retained);
    errno = saved_errno;
    return NULL;
}

int fs_iso9660_prefetch_directory(file_t directory) {
    cdrom_request_status_t status;
    cdrom_request_t *request = fs_iso9660_prefetch_directory_async(
        directory, 0, NULL, NULL);

    if(!request)
        return -1;
    if(cdrom_request_wait(request, 0, &status) < 0) {
        int saved_errno = errno;

        cdrom_request_cancel(request);
        cdrom_request_wait(request, 0, NULL);
        cdrom_request_destroy(request);
        errno = saved_errno;
        return -1;
    }

    if(status.state != CDROM_REQUEST_COMPLETE) {
        errno = status.error;
        cdrom_request_destroy(request);
        return -1;
    }

    cdrom_request_destroy(request);
    return 0;
}

static void iso_async_read_finalize(cdrom_request_t *request,
                                    const cdrom_request_status_t *status,
                                    void *data) {
    iso_async_read_t *async = data;

    mutex_lock(&fh_mutex);

    if(status->state == CDROM_REQUEST_COMPLETE && !async->fd->broken) {
        if(async->data_size < async->transfer_size) {
            memset((uint8_t *)async->buffer + async->data_size, 0,
                   async->transfer_size - async->data_size);
        }

        async->fd->ptr = async->start + async->data_size;
    }

    if(async->fd->async_request == request)
        async->fd->async_request = NULL;

    mutex_unlock(&fh_mutex);

    if(!async->callback) {
        fs_close(async->retained_fd);
        free(async);
    }
}

static void iso_async_read_complete(cdrom_request_t *request,
                                    const cdrom_request_status_t *status,
                                    void *data) {
    iso_async_read_t *async = data;

    if(async->callback)
        async->callback(request, status, async->callback_data);

    fs_close(async->retained_fd);
    free(async);
}

static int iso_async_read_make_segment(
    iso_async_read_t *async, cdrom_request_dma_segment_t *segment) {
    size_t remaining_io = async->transfer_size - async->io_delivered;
    size_t remaining_data = async->data_size - async->data_delivered;
    size_t sector_count = remaining_io / 2048;
    size_t data_bytes;
    uint64_t fad;

    if(!remaining_io || remaining_io & 2047) {
        errno = EINVAL;
        return -1;
    }

    if(sector_count > ISO_ASYNC_CHUNK_SECTORS)
        sector_count = ISO_ASYNC_CHUNK_SECTORS;

    data_bytes = sector_count * 2048;
    if(data_bytes > remaining_data)
        data_bytes = remaining_data;

    fad = (uint64_t)async->base_fad + async->io_delivered / 2048;
    if(fad > UINT32_MAX
            || cdrom_request_dma_segment_init(
                segment, async->buffer + async->io_delivered,
                (uint32_t)fad, sector_count, 0, data_bytes, true) < 0)
        return -1;

    /* ISO file data is always planned in 2,048-byte logical sectors. Refuse
       to continue if another user changed the global GD data size. */
    if(segment->io_bytes != sector_count * 2048) {
        errno = ENOTSUP;
        return -1;
    }

    return 0;
}

static int iso_async_read_continue(
    cdrom_request_t *request,
    const cdrom_request_dma_segment_t *completed,
    cdrom_request_dma_segment_t *next, void *data) {
    iso_async_read_t *async = data;

    (void)request;

    async->io_delivered += completed->io_bytes;
    async->data_delivered += completed->data_bytes;

    if(async->io_delivered == async->transfer_size)
        return 0;

    return iso_async_read_make_segment(async, next) < 0 ? -1 : 1;
}

cdrom_request_t *fs_iso9660_read_direct_async(
    file_t file, void *buffer, size_t sector_count, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data) {
    iso_async_read_t *async = NULL;
    cdrom_request_dma_segment_t first;
    cdrom_request_t *request;
    iso_fd_t *fd;
    file_t retained = FILEHND_INVALID;
    size_t requested_size;
    size_t remaining;
    size_t active;
    uint64_t fad;
    uint64_t last_fad;
    uint32_t data_extent;
    int saved_errno;

    if(sector_count > SIZE_MAX / 2048
            || (sector_count && (!buffer || ((uintptr_t)buffer & 31)))) {
        errno = EINVAL;
        return NULL;
    }

    requested_size = sector_count * 2048;

    retained = fs_dup(file);
    if(retained == FILEHND_INVALID)
        return NULL;

    if(fs_get_handler(retained) != &vh
            || !(fd = (iso_fd_t *)fs_get_handle(retained))) {
        errno = EXDEV;
        goto fail;
    }

    async = calloc(1, sizeof(*async));
    if(!async) {
        errno = ENOMEM;
        goto fail;
    }

    mutex_lock(&fh_mutex);

    if(!fd->first_extent || fd->broken) {
        errno = EBADF;
        goto fail_locked;
    }
    if(fd->dir) {
        errno = EISDIR;
        goto fail_locked;
    }
    if(iso_fd_async_busy(fd)) {
        errno = EBUSY;
        goto fail_locked;
    }
    if(fd->ptr & 2047) {
        errno = EINVAL;
        goto fail_locked;
    }
    if(fd->flags & ISO9660_FILE_MULTI_EXTENT
            || fd->file_unit_size || fd->interleave_gap) {
        errno = ENOTSUP;
        goto fail_locked;
    }
    remaining = fd->ptr < fd->size ? fd->size - fd->ptr : 0;
    async->data_size = remaining < requested_size ? remaining : requested_size;
    active = async->data_size / 2048 + !!(async->data_size % 2048);

    fad = 0;
    if(active) {
        if(iso_data_extent(fd->first_extent, fd->ext_attr_length,
                           &data_extent) < 0)
            goto fail_locked;
        fad = (uint64_t)data_extent + fd->ptr / 2048 + 150;
        last_fad = fad + active - 1;
        if(fad > UINT32_MAX || last_fad > UINT32_MAX) {
            errno = EOVERFLOW;
            goto fail_locked;
        }
    }

    async->fd = fd;
    async->retained_fd = retained;
    async->buffer = buffer;
    async->start = fd->ptr;
    async->base_fad = (uint32_t)fad;
    async->transfer_size = active * 2048;
    async->callback = callback;
    async->callback_data = callback_data;

    if(active) {
        if(iso_async_read_make_segment(async, &first) < 0)
            goto fail_locked;

        if(stream_fd)
            iso_abort_stream(false);

        request = iso_backend_submit_dma_chain(
            &first, requested_size, async->data_size, async->transfer_size,
            timeout, iso_async_read_continue, async,
            iso_async_read_finalize, async,
            callback ? iso_async_read_complete : NULL, async);
    }
    else {
        request = iso_backend_submit_noop(
            requested_size, iso_async_read_finalize, async,
            callback ? iso_async_read_complete : NULL, async);
    }
    if(!request)
        goto fail_locked;

    fd->async_request = request;
    mutex_unlock(&fh_mutex);
    return request;

fail_locked:
    saved_errno = errno;
    mutex_unlock(&fh_mutex);
    free(async);
    fs_close(retained);
    errno = saved_errno;
    return NULL;

fail:
    saved_errno = errno;
    free(async);
    if(retained != FILEHND_INVALID)
        fs_close(retained);
    errno = saved_errno;
    return NULL;
}

ssize_t fs_iso9660_read_direct(file_t file, void *buffer,
                               size_t sector_count) {
    cdrom_request_status_t status;
    cdrom_request_t *request;
    ssize_t result;

    if(sector_count > (size_t)INTPTR_MAX / 2048) {
        errno = EOVERFLOW;
        return -1;
    }

    request = fs_iso9660_read_direct_async(
        file, buffer, sector_count, 0, NULL, NULL);
    if(!request)
        return -1;

    if(cdrom_request_wait(request, 0, &status) < 0) {
        int saved_errno = errno;

        cdrom_request_cancel(request);
        cdrom_request_wait(request, 0, NULL);
        cdrom_request_destroy(request);
        errno = saved_errno;
        return -1;
    }

    if(status.state != CDROM_REQUEST_COMPLETE) {
        errno = status.error;
        result = -1;
    }
    else {
        result = (ssize_t)status.data_bytes;
    }

    cdrom_request_destroy(request);
    return result;
}

static int iso_async_byte_make_segment(
    iso_async_byte_read_t *async, cdrom_request_dma_segment_t *segment) {
    size_t remaining = async->data_size - async->delivered;
    uint32_t file_offset = async->start + async->delivered;
    size_t sector_offset = file_offset & 2047;
    uint8_t *destination = async->buffer + async->delivered;
    void *dma_buffer;
    size_t data_offset;
    size_t data_bytes;
    size_t sector_count;
    bool direct;
    uint64_t fad;

    if(!remaining) {
        errno = EINVAL;
        return -1;
    }

    if(sector_offset) {
        data_bytes = 2048 - sector_offset;
        if(data_bytes > remaining)
            data_bytes = remaining;
        data_offset = sector_offset;
        sector_count = 1;
        dma_buffer = iso_async_bounce;
        direct = false;
    }
    else if(!((uintptr_t)destination & 31) && remaining >= 2048) {
        sector_count = remaining / 2048;
        if(sector_count > ISO_ASYNC_CHUNK_SECTORS)
            sector_count = ISO_ASYNC_CHUNK_SECTORS;
        data_bytes = sector_count * 2048;
        data_offset = 0;
        dma_buffer = destination;
        direct = true;
    }
    else {
        data_bytes = remaining < ISO_ASYNC_BOUNCE_SIZE
            ? remaining : ISO_ASYNC_BOUNCE_SIZE;
        data_offset = 0;
        sector_count = data_bytes / 2048 + !!(data_bytes & 2047);
        dma_buffer = iso_async_bounce;
        direct = false;
    }

    if(!dma_buffer) {
        errno = ENOMEM;
        return -1;
    }

    fad = (uint64_t)async->base_fad + file_offset / 2048;
    if(fad > UINT32_MAX
            || cdrom_request_dma_segment_init(
                segment, dma_buffer, (uint32_t)fad, sector_count,
                data_offset, data_bytes, direct) < 0)
        return -1;

    /* ISO file data is always planned in 2,048-byte logical sectors. Refuse
       to continue if another user changed the global GD data size. */
    if(segment->io_bytes != sector_count * 2048) {
        errno = ENOTSUP;
        return -1;
    }

    return 0;
}

static int iso_async_byte_continue(
    cdrom_request_t *request,
    const cdrom_request_dma_segment_t *completed,
    cdrom_request_dma_segment_t *next, void *data) {
    iso_async_byte_read_t *async = data;

    (void)request;

    if(!completed->data_direct) {
        /* The request worker copies the shared workspace before requeueing
           this request. Its serialized continuation context makes the buffer
           safe even when chains alternate at the queue tail. */
        memcpy(async->buffer + async->delivered,
               (uint8_t *)completed->buffer + completed->data_offset,
               completed->data_bytes);
    }

    async->delivered += completed->data_bytes;
    if(async->delivered == async->data_size)
        return 0;

    return iso_async_byte_make_segment(async, next) < 0 ? -1 : 1;
}

static void iso_async_byte_finalize(cdrom_request_t *request,
                                    const cdrom_request_status_t *status,
                                    void *data) {
    iso_async_byte_read_t *async = data;

    mutex_lock(&fh_mutex);

    if(status->state == CDROM_REQUEST_COMPLETE && !async->fd->broken)
        async->fd->ptr = async->start + status->data_bytes;

    if(async->fd->async_request == request)
        async->fd->async_request = NULL;

    mutex_unlock(&fh_mutex);

    if(!async->callback) {
        fs_close(async->retained_fd);
        free(async);
    }
}

static void iso_async_byte_complete(cdrom_request_t *request,
                                    const cdrom_request_status_t *status,
                                    void *data) {
    iso_async_byte_read_t *async = data;

    if(async->callback)
        async->callback(request, status, async->callback_data);

    fs_close(async->retained_fd);
    free(async);
}

cdrom_request_t *fs_iso9660_read_async(
    file_t file, void *buffer, size_t bytes, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data) {
    iso_async_byte_read_t *async = NULL;
    cdrom_request_dma_segment_t first;
    cdrom_request_t *request;
    iso_fd_t *fd;
    file_t retained = FILEHND_INVALID;
    size_t remaining;
    size_t io_bytes = 0;
    uint64_t base_fad;
    uint64_t physical_span;
    uint64_t last_fad;
    uint32_t data_extent;
    int saved_errno;

    if(bytes && !buffer) {
        errno = EINVAL;
        return NULL;
    }

    retained = fs_dup(file);
    if(retained == FILEHND_INVALID)
        return NULL;

    if(fs_get_handler(retained) != &vh
            || !(fd = (iso_fd_t *)fs_get_handle(retained))) {
        errno = EXDEV;
        goto fail;
    }

    async = calloc(1, sizeof(*async));
    if(!async) {
        errno = ENOMEM;
        goto fail;
    }

    mutex_lock(&fh_mutex);

    if(!fd->first_extent || fd->broken) {
        errno = EBADF;
        goto fail_locked;
    }
    if(fd->dir) {
        errno = EISDIR;
        goto fail_locked;
    }
    if(iso_fd_async_busy(fd)) {
        errno = EBUSY;
        goto fail_locked;
    }
    if(fd->flags & ISO9660_FILE_MULTI_EXTENT
            || fd->file_unit_size || fd->interleave_gap) {
        errno = ENOTSUP;
        goto fail_locked;
    }

    remaining = fd->ptr < fd->size ? fd->size - fd->ptr : 0;
    async->data_size = remaining < bytes ? remaining : bytes;
    async->fd = fd;
    async->retained_fd = retained;
    async->buffer = buffer;
    async->start = fd->ptr;
    async->callback = callback;
    async->callback_data = callback_data;

    if(iso_data_extent(fd->first_extent, fd->ext_attr_length,
                       &data_extent) < 0)
        goto fail_locked;
    base_fad = (uint64_t)data_extent + 150;
    if(async->data_size) {
        physical_span = (uint64_t)(fd->ptr & 2047) + async->data_size;
        physical_span = (physical_span + 2047) & ~(uint64_t)2047;
        last_fad = base_fad
            + ((uint64_t)fd->ptr + async->data_size - 1) / 2048;

        if(base_fad > UINT32_MAX || last_fad > UINT32_MAX
                || physical_span > SIZE_MAX) {
            errno = EOVERFLOW;
            goto fail_locked;
        }

        async->base_fad = (uint32_t)base_fad;
        io_bytes = (size_t)physical_span;

        if(iso_async_byte_needs_bounce(async) &&
           iso_async_bounce_ensure() < 0)
            goto fail_locked;

        if(iso_async_byte_make_segment(async, &first) < 0)
            goto fail_locked;

        if(stream_fd)
            iso_abort_stream(false);

        request = iso_backend_submit_dma_chain(
            &first, bytes, async->data_size, io_bytes, timeout,
            iso_async_byte_continue, async, iso_async_byte_finalize, async,
            callback ? iso_async_byte_complete : NULL, async);
    }
    else {
        request = iso_backend_submit_noop(
            bytes, iso_async_byte_finalize, async,
            callback ? iso_async_byte_complete : NULL, async);
    }

    if(!request)
        goto fail_locked;

    fd->async_request = request;
    mutex_unlock(&fh_mutex);
    return request;

fail_locked:
    saved_errno = errno;
    mutex_unlock(&fh_mutex);
    free(async);
    fs_close(retained);
    errno = saved_errno;
    return NULL;

fail:
    saved_errno = errno;
    free(async);
    if(retained != FILEHND_INVALID)
        fs_close(retained);
    errno = saved_errno;
    return NULL;
}

static void iso_async_stream_finalize(
    cdrom_stream_session_t *session,
    const cdrom_stream_session_status_t *status, void *data) {
    iso_async_stream_t *async = data;

    mutex_lock(&fh_mutex);

    if(!async->fd->broken)
        async->fd->ptr = async->start + status->completed_bytes;

    if(async->fd->stream_session == session)
        async->fd->stream_session = NULL;

    mutex_unlock(&fh_mutex);
    fs_close(async->retained_fd);
    free(async);
}

cdrom_stream_session_t *fs_iso9660_stream_start(
    file_t file, size_t sector_count, uint32_t start_timeout,
    uint32_t idle_timeout) {
    iso_async_stream_t *async = NULL;
    cdrom_stream_session_t *session;
    iso_fd_t *fd;
    file_t retained = FILEHND_INVALID;
    size_t requested_size;
    size_t remaining;
    size_t data_size;
    size_t active;
    uint64_t fad;
    uint64_t last_fad;
    uint32_t data_extent;
    int saved_errno;

    if(!sector_count || !idle_timeout || sector_count > SIZE_MAX / 2048) {
        errno = EINVAL;
        return NULL;
    }
    requested_size = sector_count * 2048;

    retained = fs_dup(file);
    if(retained == FILEHND_INVALID)
        return NULL;

    if(fs_get_handler(retained) != &vh
            || !(fd = (iso_fd_t *)fs_get_handle(retained))) {
        errno = EXDEV;
        goto fail;
    }

    async = calloc(1, sizeof(*async));
    if(!async) {
        errno = ENOMEM;
        goto fail;
    }

    mutex_lock(&fh_mutex);

    if(!fd->first_extent || fd->broken) {
        errno = EBADF;
        goto fail_locked;
    }
    if(fd->dir) {
        errno = EISDIR;
        goto fail_locked;
    }
    if(iso_fd_async_busy(fd)) {
        errno = EBUSY;
        goto fail_locked;
    }
    if(fd->ptr & 2047) {
        errno = EINVAL;
        goto fail_locked;
    }
    if(fd->flags & ISO9660_FILE_MULTI_EXTENT
            || fd->file_unit_size || fd->interleave_gap) {
        errno = ENOTSUP;
        goto fail_locked;
    }

    remaining = fd->ptr < fd->size ? fd->size - fd->ptr : 0;
    data_size = remaining < requested_size ? remaining : requested_size;
    if(!data_size) {
        errno = ENODATA;
        goto fail_locked;
    }
    active = data_size / 2048 + !!(data_size & 2047);

    if(iso_data_extent(fd->first_extent, fd->ext_attr_length,
                       &data_extent) < 0)
        goto fail_locked;
    fad = (uint64_t)data_extent + fd->ptr / 2048 + 150;
    last_fad = fad + active - 1;
    if(fad > UINT32_MAX || last_fad > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail_locked;
    }

    async->fd = fd;
    async->retained_fd = retained;
    async->start = fd->ptr;

    if(stream_fd)
        iso_abort_stream(false);

    session = cdrom_stream_session_start_internal(
        (uint32_t)fad, active, 2048, data_size,
        iso_backend == FS_ISO9660_BACKEND_DIRECT && !start_timeout
            ? ISO_DIRECT_COMMAND_TIMEOUT_MS : start_timeout,
        idle_timeout,
        iso_backend == FS_ISO9660_BACKEND_DIRECT
            ? CDROM_REQUEST_BACKEND_DIRECT : CDROM_REQUEST_BACKEND_BIOS,
        iso_direct_sector_type, iso_async_stream_finalize, async);
    if(!session)
        goto fail_locked;

    fd->stream_session = session;
    mutex_unlock(&fh_mutex);
    return session;

fail_locked:
    saved_errno = errno;
    mutex_unlock(&fh_mutex);
    free(async);
    fs_close(retained);
    errno = saved_errno;
    return NULL;

fail:
    saved_errno = errno;
    free(async);
    if(retained != FILEHND_INVALID)
        fs_close(retained);
    errno = saved_errno;
    return NULL;
}

static void iso_async_preseek_finalize(cdrom_request_t *request,
                                       const cdrom_request_status_t *status,
                                       void *data) {
    iso_async_preseek_t *async = data;

    (void)status;

    mutex_lock(&fh_mutex);

    if(async->fd->async_request == request)
        async->fd->async_request = NULL;

    mutex_unlock(&fh_mutex);

    if(!async->callback) {
        fs_close(async->retained_fd);
        free(async);
    }
}

static void iso_async_preseek_complete(cdrom_request_t *request,
                                       const cdrom_request_status_t *status,
                                       void *data) {
    iso_async_preseek_t *async = data;

    if(async->callback)
        async->callback(request, status, async->callback_data);

    fs_close(async->retained_fd);
    free(async);
}

cdrom_request_t *fs_iso9660_preseek_async(
    file_t file, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data) {
    iso_async_preseek_t *async = NULL;
    cdrom_request_t *request;
    iso_fd_t *fd;
    file_t retained = FILEHND_INVALID;
    uint64_t fad;
    uint32_t data_extent;
    int saved_errno;

    retained = fs_dup(file);
    if(retained == FILEHND_INVALID)
        return NULL;

    if(fs_get_handler(retained) != &vh
            || !(fd = (iso_fd_t *)fs_get_handle(retained))) {
        errno = EXDEV;
        goto fail;
    }

    async = calloc(1, sizeof(*async));
    if(!async) {
        errno = ENOMEM;
        goto fail;
    }

    mutex_lock(&fh_mutex);

    if(!fd->first_extent || fd->broken) {
        errno = EBADF;
        goto fail_locked;
    }
    if(fd->dir) {
        errno = EISDIR;
        goto fail_locked;
    }
    if(iso_fd_async_busy(fd)) {
        errno = EBUSY;
        goto fail_locked;
    }
    if(fd->flags & ISO9660_FILE_MULTI_EXTENT
            || fd->file_unit_size || fd->interleave_gap) {
        errno = ENOTSUP;
        goto fail_locked;
    }
    if(fd->ptr >= fd->size) {
        errno = ENODATA;
        goto fail_locked;
    }

    if(iso_data_extent(fd->first_extent, fd->ext_attr_length,
                       &data_extent) < 0)
        goto fail_locked;
    fad = (uint64_t)data_extent + fd->ptr / 2048 + 150;
    if(fad > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail_locked;
    }

    async->fd = fd;
    async->retained_fd = retained;
    async->callback = callback;
    async->callback_data = callback_data;

    if(stream_fd)
        iso_abort_stream(false);

    if(iso_backend == FS_ISO9660_BACKEND_DIRECT) {
        request = gdrom_direct_seek_async_internal(
            (uint32_t)fad,
            timeout ? timeout : ISO_DIRECT_COMMAND_TIMEOUT_MS, NULL,
            iso_async_preseek_finalize, async,
            callback ? iso_async_preseek_complete : NULL, async);
    }
    else {
        request = cdrom_seek_async_internal(
            (uint32_t)fad, timeout, iso_async_preseek_finalize, async,
            callback ? iso_async_preseek_complete : NULL, async);
    }
    if(!request)
        goto fail_locked;

    fd->async_request = request;
    mutex_unlock(&fh_mutex);
    return request;

fail_locked:
    saved_errno = errno;
    mutex_unlock(&fh_mutex);
    free(async);
    fs_close(retained);
    errno = saved_errno;
    return NULL;

fail:
    saved_errno = errno;
    free(async);
    if(retained != FILEHND_INVALID)
        fs_close(retained);
    errno = saved_errno;
    return NULL;
}

static int iso_rewinddir(void * h) {
    iso_fd_t *fd = (iso_fd_t *)h;

    if(fd->first_extent == 0 || !fd->dir || fd->broken) {
        errno = EBADF;
        return -1;
    }

    /* Rewind to the beginning of the directory. */
    fd->ptr = 0;
    return 0;
}

int iso_reset(void) {
    iso_break_all();
    bclear();
    iso_abort_stream(false);
    percd_done = false;
    return 0;
}

/* Media callbacks run in thread context. Conservatively mark the mount stale
   for every significant transition, including ERROR/RECOVERED: the drive may
   have lost or replaced media while its state was unreadable. The next
   foreground operation performs the existing reset/remount sequence. */
static int iso_media_event_hnd = -1;
static mutex_t iso_media_event_mutex;
static bool iso_media_monitor_warning;

static void iso_media_event(const cdrom_media_event_t *event, void *data) {
    (void)data;

    /* The generation is a retry budget, not disc identity: each significant
       event permits one future fatal-state reset. Ordinary insert/remove and
       change events still use the non-resetting probe/remount path. */
    mutex_lock(&backend_mutex);
    if(++iso_media_generation == 0) {
        iso_media_generation = 1;
        iso_direct_fatal_recovery_generation = 0;
    }
    if(iso_backend == FS_ISO9660_BACKEND_BIOS
            && (event->type == CDROM_MEDIA_EVENT_INSERTED
                || event->type == CDROM_MEDIA_EVENT_CHANGED))
        iso_bios_recognition_pending = true;
    mutex_unlock(&backend_mutex);

    percd_done = false;
}

static int iso_media_monitor_ensure(void) {
    int handle;

    mutex_lock(&iso_media_event_mutex);
    if(iso_media_event_hnd >= 0) {
        mutex_unlock(&iso_media_event_mutex);
        return 0;
    }

    handle = cdrom_media_event_handler_add(iso_media_event, NULL);
    if(handle < 0) {
        if(!iso_media_monitor_warning) {
            dbglog(DBG_WARNING,
                   "fs_iso9660: automatic media invalidation unavailable\n");
            iso_media_monitor_warning = true;
        }
        mutex_unlock(&iso_media_event_mutex);
        return -1;
    }

    iso_media_event_hnd = handle;
    iso_media_monitor_warning = false;
    mutex_unlock(&iso_media_event_mutex);
    return 0;
}

static int iso_stat(vfs_handler_t *vfs, const char *path, struct stat *st,
                    int flag) {
    mode_t md;
    iso_dirent_t de;
    size_t len = strlen(path);

    (void)vfs;
    (void)flag;

    /* Root directory of cd */
    if(len == 0 || (len == 1 && *path == '/')) {
        memset(st, 0, sizeof(struct stat));
        st->st_dev = (dev_t)('c' | ('d' << 8));
        st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR |
            S_IXGRP | S_IXOTH;
        st->st_size = -1;
        st->st_nlink = 2;

        return 0;
    }

    /* Do this only when we need to (this is still imperfect) */
    if(!percd_done && init_percd() < 0) {
        errno = ENODEV;
        return -1;
    }

    percd_done = true;

    /* First try opening as a file */
    if(find_object_path(path, 0, &root_dirent, &de) == 0) {
        md = S_IFREG;
    }
    else {
        /* If we couldn't get it as a file, try as a directory. */
        if(find_object_path(path, 1, &root_dirent, &de) < 0) {
            errno = ENOENT;
            return -1;
        }
        md = S_IFDIR;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_dev = (dev_t)('c' | ('d' << 8));
    st->st_mode = md | S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH;
    st->st_size = (md == S_IFDIR) ? -1 : (int)iso_733(de.size);
    st->st_nlink = (md == S_IFDIR) ? 2 : 1;
    st->st_blksize = 512;

    return 0;
}

static int iso_fcntl(void *h, int cmd, va_list ap) {
    iso_fd_t *fd = (iso_fd_t *)h;
    int rv = -1;

    (void)ap;

    if(!fd->first_extent || fd->broken) {
        errno = EBADF;
        return -1;
    }

    switch(cmd) {
        case F_GETFL:
            rv = O_RDONLY;

            if(fd->dir)
                rv |= O_DIR;

            break;

        case F_SETFL:
        case F_GETFD:
        case F_SETFD:
            rv = 0;
            break;

        default:
            errno = EINVAL;
    }

    return rv;
}

static int iso_fstat(void *h, struct stat *st) {
    iso_fd_t *fd = (iso_fd_t *)h;

    if(!fd->first_extent || fd->broken) {
        errno = EBADF;
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_dev = 'c' | ('d' << 8);
    st->st_mode = S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH;
    st->st_mode |= fd->dir ? S_IFDIR : S_IFREG;
    st->st_size = fd->dir ? -1 : (int)fd->size;
    st->st_nlink = fd->dir ? 2 : 1;
    st->st_blksize = 512;

    return 0;
}

/* Put everything together */
static vfs_handler_t vh = {
    /* Name handler */
    {
        "/cd",          /* name */
        0,              /* tbfi */
        0x00010000,     /* Version 1.0 */
        0,              /* flags */
        NMMGR_TYPE_VFS, /* VFS handler */
        NMMGR_LIST_INIT
    },

    0, NULL,            /* no caching, privdata */

    iso_open,
    iso_close,
    iso_read,
    NULL,
    iso_seek,
    iso_tell,
    iso_total,
    iso_readdir,
    iso_ioctl,
    NULL,
    NULL,
    NULL,
    NULL,
    iso_stat,
    NULL,
    NULL,
    iso_fcntl,
    NULL,               /* poll */
    NULL,               /* link */
    NULL,               /* symlink */
    NULL,               /* seek64 */
    NULL,               /* tell64 */
    NULL,               /* total64 */
    NULL,               /* readlink */
    iso_rewinddir,
    iso_fstat
};

/* Initialize the file system */
void fs_iso9660_init(void) {
    int i;

    if(iso_initialized)
        return;

    /* Init the linked list */
    TAILQ_INIT(&iso_fd_queue);

    /* Init thread mutexes */
    mutex_init(&cache_mutex, MUTEX_TYPE_NORMAL);
    mutex_init(&fh_mutex, MUTEX_TYPE_NORMAL);
    mutex_init(&directory_snapshot_mutex, MUTEX_TYPE_NORMAL);
    mutex_init(&backend_mutex, MUTEX_TYPE_NORMAL);
    mutex_init(&iso_media_event_mutex, MUTEX_TYPE_NORMAL);

    iso_backend = FS_ISO9660_BACKEND_BIOS;
    iso_backend_locked = false;
    iso_direct_sector_type = GDROM_DIRECT_SECTOR_MODE1;
    iso_media_generation = 1;
    iso_direct_fatal_recovery_generation = 0;
    iso_bios_recognition_pending = false;
    iso_media_event_hnd = -1;
    iso_media_monitor_warning = false;
    cdrom_media_monitor_use_direct(false);

    /* Allocate cache block space, properly aligned for DMA access */
    cache_data = aligned_alloc(32, 2 * NUM_CACHE_BLOCKS * 2048);
    caches = malloc(2 * NUM_CACHE_BLOCKS * sizeof(cache_block_t));

    if(!cache_data || !caches) {
        dbglog(DBG_ERROR,
               "fs_iso9660_init: unable to allocate cache workspace\n");
        goto fail;
    }

    for(i = 0; i < NUM_CACHE_BLOCKS; i++) {
        icache[i] = &caches[i * 2];
        icache[i]->data = &cache_data[i * 2 * 2048];
        icache[i]->sector = -1;
        dcache[i] = &caches[i * 2 + 1];
        dcache[i]->data = &cache_data[i * 2 * 2048 + 2048];
        dcache[i]->sector = -1;
    }

    percd_done = false;

    /* Register with VFS */
    if(nmmgr_handler_add(&vh.nmmgr) < 0) {
        dbglog(DBG_ERROR,
               "fs_iso9660_init: unable to register /cd filesystem\n");
        goto fail;
    }

    iso_initialized = true;
    return;

fail:
    if(iso_media_event_hnd >= 0)
        (void)cdrom_media_event_handler_remove(iso_media_event_hnd);
    iso_media_event_hnd = -1;
    free(cache_data);
    free(caches);
    free(iso_async_bounce);
    cache_data = NULL;
    caches = NULL;
    iso_async_bounce = NULL;
    mutex_destroy(&cache_mutex);
    mutex_destroy(&fh_mutex);
    mutex_destroy(&directory_snapshot_mutex);
    mutex_destroy(&backend_mutex);
    mutex_destroy(&iso_media_event_mutex);
}

/* De-init the file system */
void fs_iso9660_shutdown(void) {
    if(!iso_initialized)
        return;

    /* arch_auto_shutdown() reaches this after fs_shutdown() has detached the
       descriptor table and cdrom_shutdown() has drained request finalizers.
       A custom shutdown sequence must provide the same quiescence. */

    /* cdrom_shutdown() normally ran first and cleared all media handlers. A
       custom shutdown order may leave ours registered, so remove it when it
       is still present. */
    mutex_lock(&iso_media_event_mutex);
    if(iso_media_event_hnd >= 0)
        (void)cdrom_media_event_handler_remove(iso_media_event_hnd);
    iso_media_event_hnd = -1;
    mutex_unlock(&iso_media_event_mutex);

    /* Stop new path operations and wait for existing retained lookups before
       dismantling caches and mutexes used by handler entry points. */
    nmmgr_handler_remove(&vh.nmmgr);

    /* Dealloc cache block space */
    free(cache_data);
    free(caches);
    free(iso_async_bounce);
    cache_data = NULL;
    caches = NULL;
    iso_async_bounce = NULL;
    directory_snapshot_clear();

    /* Free muteces */
    mutex_destroy(&cache_mutex);
    mutex_destroy(&fh_mutex);
    mutex_destroy(&directory_snapshot_mutex);
    mutex_destroy(&backend_mutex);
    mutex_destroy(&iso_media_event_mutex);

    iso_initialized = false;
}
