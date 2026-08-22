/* KallistiOS ##version##

   dc/pvr/pvr_txr.h
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2014 Lawrence Sebald
   Copyright (C) 2023 Ruslan Rostovtsev
   Copyright (C) 2024 Falco Girgis
   Copyright (C) 2024 Andress Barajas
   Copyright (C) 2026 Joseph Black
*/

/** \file       dc/pvr/pvr_txr.h
    \brief      Texture management with the PVR 3D API
    \ingroup    pvr_txr_mgmt

    \author Megan Potter
    \author Roger Cattermole
    \author Paul Boese
    \author Brian Paul
    \author Lawrence Sebald
    \author Benoit Miller
    \author Ruslan Rostovtsev
    \author Falco Girgis
    \author Joseph Black
*/

#ifndef __DC_PVR_PVR_TEXTURE_H
#define __DC_PVR_PVR_TEXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <kos/img.h>

/** \defgroup pvr_txr_mgmt      Texturing
    \brief                      API for managing PowerVR textures
    \ingroup                    pvr

    Helper functions for handling texture tasks of various kinds.
*/

/** \brief   Set the global stride width for non-power-of-two textures in PVR RAM.
    \ingroup pvr_txr_mgmt

    This function configures the register `PVR_TEXTURE_MODULO`, whose
    first five bits define the row width in VRAM for non-power-of-two
    textures. The setting applies to all textures rendered with the
    `PVR_TXRFMT_X32_STRIDE` flag in the same frame.

    The stride width configured here is **only supported for textures
    with widths that are multiples of 32 pixels** and up to a maximum
    of 992 pixels.

    \warning
    - Textures that are twiddled cannot use the `PVR_TXRFMT_X32_STRIDE`
      flag so the stride set here will not apply to them. This includes
      all paletted and mipmap textures.

    \param  texture_width   The width of the texture in pixels. Must be a
                            multiple of 32 and up to 992 pixels.

    \sa pvr_txr_get_stride()
*/
void pvr_txr_set_stride(size_t texture_width);

/** \brief   Get the current texture stride width in pixels as set in the PVR.
    \ingroup pvr_txr_mgmt

    This function reads the `PVR_TEXTURE_MODULO` register and calculates the
    texture stride width in pixels. The value returned is the width in pixels
    that has been configured for all textures using the `PVR_TXRFMT_X32_STRIDE`
    flag in the same frame.

    The stride width is computed by taking the current multiplier in
    `PVR_TEXTURE_MODULO` (which stores the width divided by 32), and
    multiplying it back by 32 to return the full width in pixels.

    \return                 The current texture stride width in pixels.
                            Or 0 if not set
    \sa pvr_txr_set_stride()
*/
size_t pvr_txr_get_stride(void);

/** \brief   Load raw texture data from an SH-4 buffer into PVR RAM.
    \ingroup pvr_txr_mgmt

    This essentially just acts as a memcpy() from main RAM to PVR RAM, using
    the Store Queues and 64-bit TA bus.

    \param  src             The location in main RAM holding the texture.
    \param  dst             The location in PVR RAM to copy to.
    \param  count           The size of the texture in bytes (must be a multiple
                            of 32).
*/
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count);

/** \defgroup pvr_txrload_constants     Flags
    \brief                              Texture loading constants
    \ingroup                            pvr_txr_mgmt

    These are constants for the flags parameter to pvr_txr_load_ex() or
    pvr_txr_load_kimg().

    @{
*/
#define PVR_TXRLOAD_4BPP            0x01    /**< \brief 4BPP format */
#define PVR_TXRLOAD_8BPP            0x02    /**< \brief 8BPP format */
#define PVR_TXRLOAD_16BPP           0x03    /**< \brief 16BPP format */
#define PVR_TXRLOAD_FMT_MASK        0x0f    /**< \brief Bits used for basic formats */

#define PVR_TXRLOAD_VQ_LOAD         0x10    /**< \brief Do VQ encoding (not supported yet, if ever) */
#define PVR_TXRLOAD_INVERT_Y        0x20    /**< \brief Invert the Y axis while loading */
#define PVR_TXRLOAD_FMT_VQ          0x40    /**< \brief Texture is already VQ encoded */
#define PVR_TXRLOAD_FMT_TWIDDLED    0x80    /**< \brief Texture is already twiddled */
#define PVR_TXRLOAD_FMT_NOTWIDDLE   0x80    /**< \brief Don't twiddle the texture while loading */
#define PVR_TXRLOAD_DMA             0x8000  /**< \brief Use DMA to load the texture */
#define PVR_TXRLOAD_NONBLOCK        0x4000  /**< \brief Use non-blocking loads (only for DMA) */
#define PVR_TXRLOAD_SQ              0x2000  /**< \brief Use Store Queues to load */
/** @} */

/** \brief   Load texture data from an SH-4 buffer into PVR RAM, twiddling it in
             the process.
    \ingroup pvr_txr_mgmt

    This compatibility entry point uses pvr_txr_load_ex_checked() and asserts
    when the checked operation fails. New code should call the checked form so
    invalid dimensions, unsupported conversion, and transfer-alignment errors
    can be handled without an assertion.

    This will be slower than using pvr_txr_load() in pretty much all cases, so
    unless you need to twiddle your texture, just use that instead.

    \param  src             The location to copy from.
    \param  dst             The location to copy to.
    \param  w               The width of the texture, in pixels.
    \param  h               The height of the texture, in pixels.
    \param  flags           Some set of flags, ORed together.

    \see    pvr_txrload_constants
*/
void pvr_txr_load_ex(const void *src, pvr_ptr_t dst,
                     uint32_t w, uint32_t h, uint32_t flags);

/** \brief Checked form of pvr_txr_load_ex().
    \ingroup pvr_txr_mgmt

    Linear input is converted to twiddled storage unless
    PVR_TXRLOAD_FMT_TWIDDLED is set. Pre-twiddled or pre-encoded VQ input can be
    copied with CPU, store-queue, or blocking DMA transfers. Conversion and
    vertical inversion use CPU writes because they are not contiguous transfer
    operations. Runtime VQ encoding and nonblocking DMA are not performed by
    this synchronous entry point.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_load_ex_checked(const void *src, pvr_ptr_t dst,
                            uint32_t w, uint32_t h, uint32_t flags);

/** \brief   Load a KOS Platform Independent Image (subject to constraint
             checking).
    \ingroup pvr_txr_mgmt

    This function loads a KOS Platform Independent image to the PVR's RAM with
    the specified set of flags. This function, unlike pvr_txr_load_ex() supports
    everything in the flags available, other than what's explicitly marked as
    not supported.

    \param  img             The image to load.
    \param  dst             The location to copy to.
    \param  flags           Some set of flags, ORed together.

    \see    pvr_txrload_constants
    \note                   Unless you explicitly tell this function to not
                            twiddle the texture (by ORing
                            \ref PVR_TXRLOAD_FMT_NOTWIDDLE or it's equivalent
                            \ref PVR_TXRLOAD_FMT_TWIDDLED with flags), this
                            function will twiddle the texture while loading.
                            Keep that in mind when setting the texture format in
                            polygon headers later.
    \note                   You cannot specify both
                            \ref PVR_TXRLOAD_FMT_NOTWIDDLE (or equivalently
                            \ref PVR_TXRLOAD_FMT_TWIDDLED) and
                            \ref PVR_TXRLOAD_INVERT_Y in the flags.
    \note                   DMA and Store Queue based loading is not available
                            from this function if it twiddles the texture while
                            loading.
*/
void pvr_txr_load_kimg(const kos_img_t *img, pvr_ptr_t dst, uint32_t flags);

/** \defgroup pvr_txr_surfaces Checked texture surfaces
    \brief                      Caller-owned texture layout metadata
    \ingroup                    pvr_txr_mgmt

    Texture surfaces describe the storage occupied by one texture without
    introducing a second VRAM allocator. Allocation uses pvr_mem_malloc(), and
    a caller can instead bind an existing VRAM range. No thread, queue, or
    permanent workspace is created by this API. The descriptor records storage
    but does not track renderer use; applications must order writes so a region
    is not modified while the PVR is sampling it.

    @{ */

/** \brief Texture pixel formats understood by checked surface operations. */
typedef enum pvr_txr_surface_format {
    PVR_TXR_SURFACE_ARGB1555 = 0, /**< 16-bit ARGB1555. */
    PVR_TXR_SURFACE_RGB565,       /**< 16-bit RGB565. */
    PVR_TXR_SURFACE_ARGB4444,     /**< 16-bit ARGB4444. */
    PVR_TXR_SURFACE_YUV422,       /**< 16-bit YUV422. */
    PVR_TXR_SURFACE_BUMP,         /**< 16-bit bump-map data. */
    PVR_TXR_SURFACE_PAL4BPP,      /**< Packed 4-bit palette indices. */
    PVR_TXR_SURFACE_PAL8BPP       /**< 8-bit palette indices. */
} pvr_txr_surface_format_t;

/** \brief Texture storage layouts understood by checked surface operations. */
typedef enum pvr_txr_surface_layout {
    PVR_TXR_SURFACE_TWIDDLED = 0, /**< Morton-ordered texture data. */
    PVR_TXR_SURFACE_LINEAR,       /**< Linear power-of-two rows. */
    PVR_TXR_SURFACE_STRIDE,       /**< Linear rows with an X32 stride. */
    PVR_TXR_SURFACE_VQ            /**< Full 2048-byte codebook plus indices. */
} pvr_txr_surface_layout_t;

/** \brief Blocking transfer method for preformatted texture bytes. */
typedef enum pvr_txr_transfer {
    PVR_TXR_TRANSFER_CPU = 0, /**< Copy through the CPU-visible VRAM alias. */
    PVR_TXR_TRANSFER_SQ,      /**< Copy with store queues. */
    PVR_TXR_TRANSFER_DMA      /**< Copy with blocking PVR DMA. */
} pvr_txr_transfer_t;

/** \brief Macroblock format accepted by the hardware YUV converter. */
typedef enum pvr_txr_yuv_format {
    PVR_TXR_YUV420 = 0, /**< 384 bytes per 16x16 macroblock. */
    PVR_TXR_YUV422      /**< 512 bytes per 16x16 macroblock. */
} pvr_txr_yuv_format_t;

/** \brief State of an asynchronous texture transfer request. */
typedef enum pvr_txr_request_state {
    PVR_TXR_REQUEST_DMA = 0,    /**< Source DMA is active. */
    PVR_TXR_REQUEST_CONVERTING, /**< YUV DMA ended; conversion remains. */
    PVR_TXR_REQUEST_COMPLETE,   /**< Destination data is complete. */
    PVR_TXR_REQUEST_FAILED,     /**< Transfer ended with an error. */
    PVR_TXR_REQUEST_CANCELLED   /**< PVR shutdown cancelled the transfer. */
} pvr_txr_request_state_t;

/** \brief Coherent snapshot of an asynchronous texture transfer. */
typedef struct pvr_txr_request_status {
    pvr_txr_request_state_t state; /**< Current request state. */
    size_t requested_bytes;        /**< Source bytes submitted to DMA. */
    size_t completed_bytes;        /**< Source bytes accepted by DMA. */
    uint32_t requested_macroblocks; /**< Expected YUV blocks, otherwise zero. */
    uint32_t completed_macroblocks; /**< Stored YUV blocks, otherwise zero. */
    int result;                     /**< Zero or the terminal errno value. */
    uint32_t detail;                /**< Zero or a pvr_fault_t detail flag. */
} pvr_txr_request_status_t;

/** \brief Opaque asynchronous texture transfer request. */
typedef struct pvr_txr_request pvr_txr_request_t;

/** \brief Storage information for one logical mip level. */
typedef struct pvr_txr_level_info {
    uint32_t width;   /**< Width of this level in texels. */
    uint32_t height;  /**< Height of this level in texels. */
    size_t offset;    /**< Byte offset from the surface's VRAM address. */
    size_t byte_size; /**< Encoded byte count for this level. */
} pvr_txr_level_info_t;

/** \brief Caller-owned description of one texture allocation. */
typedef struct pvr_txr_surface {
    pvr_ptr_t vram;                   /**< Bound VRAM address, or NULL. */
    size_t capacity;                  /**< Bytes available at \a vram. */
    size_t byte_size;                 /**< Total bytes required. */
    size_t codebook_size;             /**< VQ codebook bytes, otherwise zero. */
    size_t data_size;                 /**< Texture bytes after the codebook. */
    uint32_t width;                   /**< Top-level storage width. */
    uint32_t height;                  /**< Top-level storage height. */
    uint16_t mip_levels;              /**< Number of addressable levels. */
    pvr_txr_surface_format_t format;  /**< Pixel format. */
    pvr_txr_surface_layout_t layout;  /**< Storage layout. */
    bool mipmapped;                   /**< Whether a complete mip chain exists. */
    bool owns_vram;                   /**< Whether release frees \a vram. */
} pvr_txr_surface_t;

/** \brief Calculate checked texture metadata without allocating VRAM.

    Width and height must be powers of two from 8 through 1024, except that an
    X32-stride surface accepts widths from 32 through 992 in multiples of 32.
    Mipmapped surfaces must be square. Palette formats are twiddled and cannot
    be combined with linear or X32-stride storage. VQ currently uses the
    hardware's full 2048-byte codebook and 16-bit texel formats.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_init(pvr_txr_surface_t *surface, uint32_t width,
                         uint32_t height, pvr_txr_surface_format_t format,
                         pvr_txr_surface_layout_t layout, bool mipmapped);

/** \brief Initialize a surface and allocate its storage with pvr_mem_malloc().
    \warning Release an existing owned surface before reinitializing it, and do
             not copy an owning descriptor because each allocation must have
             exactly one owner.
    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_alloc(pvr_txr_surface_t *surface, uint32_t width,
                          uint32_t height, pvr_txr_surface_format_t format,
                          pvr_txr_surface_layout_t layout, bool mipmapped);

/** \brief Initialize a surface over caller-provided VRAM.

    The address must use the 64-bit CPU-visible VRAM alias, be eight-byte
    aligned, and have enough remaining VRAM and declared \a capacity for the
    computed surface size. The caller retains ownership of the VRAM. Release an
    existing owned surface before rebinding it.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_bind(pvr_txr_surface_t *surface, pvr_ptr_t vram,
                         size_t capacity, uint32_t width, uint32_t height,
                         pvr_txr_surface_format_t format,
                         pvr_txr_surface_layout_t layout, bool mipmapped);

/** \brief Release owned VRAM and clear a surface descriptor. */
void pvr_txr_surface_release(pvr_txr_surface_t *surface);

/** \brief Query the byte range occupied by one logical mip level.

    Level zero is the largest level even though mipmapped texture storage places
    smaller levels first.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_get_level(const pvr_txr_surface_t *surface,
                              uint32_t level, pvr_txr_level_info_t *info);

/** \brief Build the PVR_TXRFMT_* word for a surface.

    Palette-bank selection is intentionally not included and can be ORed into
    the returned value by the caller. Mipmap enable is context state rather than
    a texture-format bit and must be set separately. X32-stride users must also
    configure the shared stride with pvr_txr_set_stride().

    \return The format word, or UINT32_MAX with errno set for an invalid
            descriptor.
*/
uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *surface);

/** \brief Upload one complete, already encoded surface image.

    The source byte order must match the surface layout, including its VQ
    codebook and mip padding where present.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_upload(const pvr_txr_surface_t *surface, const void *src,
                           size_t byte_size, pvr_txr_transfer_t transfer);

/** \brief Upload a checked byte range from an already encoded image.

    Store-queue and DMA transfers require source, destination, offset, and byte
    count to be 32-byte aligned. CPU transfers accept arbitrary byte ranges.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_upload_part(const pvr_txr_surface_t *surface,
                                size_t offset, const void *src,
                                size_t byte_size,
                                pvr_txr_transfer_t transfer);

/** \brief Upload one already encoded mip level. */
int pvr_txr_surface_upload_level(const pvr_txr_surface_t *surface,
                                 uint32_t level, const void *src,
                                 size_t byte_size,
                                 pvr_txr_transfer_t transfer);

/** \brief Replace the complete codebook of a VQ surface. */
int pvr_txr_surface_upload_codebook(const pvr_txr_surface_t *surface,
                                    const void *src, size_t byte_size,
                                    pvr_txr_transfer_t transfer);

/** \brief Start a complete asynchronous DMA upload.

    Submission never waits for the shared PVR DMA channel. It returns EBUSY if
    another checked texture or scene-list DMA owns the channel. An accepted
    request advances from interrupt context without an application pump or
    worker thread. The source memory, surface allocation, and descriptor must
    remain valid until the request is terminal. Legacy raw PVR DMA calls retain
    their established external-serialization requirement.

    \return 0 with a request in \a request, or -1 with errno set.
*/
int pvr_txr_surface_upload_async(const pvr_txr_surface_t *surface,
                                 const void *src, size_t byte_size,
                                 pvr_txr_request_t **request);

/** \brief Start an asynchronous DMA upload of an encoded byte range.

    Source, destination, offset, and byte count must be 32-byte aligned. The
    request follows the ownership and immediate-admission rules of
    pvr_txr_surface_upload_async().
*/
int pvr_txr_surface_upload_part_async(const pvr_txr_surface_t *surface,
                                      size_t offset, const void *src,
                                      size_t byte_size,
                                      pvr_txr_request_t **request);

/** \brief Start an asynchronous DMA upload of one encoded mip level. */
int pvr_txr_surface_upload_level_async(const pvr_txr_surface_t *surface,
                                       uint32_t level, const void *src,
                                       size_t byte_size,
                                       pvr_txr_request_t **request);

/** \brief Start an asynchronous DMA replacement of a complete VQ codebook. */
int pvr_txr_surface_upload_codebook_async(const pvr_txr_surface_t *surface,
                                          const void *src, size_t byte_size,
                                          pvr_txr_request_t **request);

/** \brief Validate a YUV surface and calculate its encoded source size.

    The destination must be a non-mipmapped, linear YUV422 surface whose width
    and height are multiples of 16 from 16 through 1024. Input consists of
    complete 16x16 macroblocks in row-major order. Each YUV420 macroblock is
    U64, V64, then Y256 bytes; each YUV422 macroblock is U64, V64, Y128,
    U64, V64, then Y128 bytes.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_yuv_input_size(const pvr_txr_surface_t *surface,
                                   pvr_txr_yuv_format_t format,
                                   size_t *byte_size);

/** \brief Convert and upload a complete macroblock-ordered YUV image.

    Completion means both source DMA and the separate YUV conversion operation
    are terminal. The input size must exactly match
    pvr_txr_surface_yuv_input_size(). Submission is immediate-only and follows
    the same lifetime rules as pvr_txr_surface_upload_async().

    \return 0 with a request in \a request, or -1 with errno set.
*/
int pvr_txr_surface_yuv_upload_async(const pvr_txr_surface_t *surface,
                                     pvr_txr_yuv_format_t format,
                                     const void *src, size_t byte_size,
                                     pvr_txr_request_t **request);

/** \brief Copy a coherent asynchronous request status snapshot.

    Byte progress is sampled from channel 2 while DMA is active. YUV macroblock
    progress is sampled from the converter register and is therefore
    hardware-granular rather than a scheduling guarantee. Progress describes
    hardware consumption; destination data is safe to use only after successful
    completion.
*/
int pvr_txr_request_get_status(const pvr_txr_request_t *request,
                               pvr_txr_request_status_t *status);

/** \brief Wait for a texture request to reach a terminal state.

    A timeout of zero waits indefinitely. Timing out does not cancel the
    hardware transfer. This function is only valid in ordinary thread context.

    \return 0 on successful completion, or -1 with errno set.
*/
int pvr_txr_request_wait(pvr_txr_request_t *request, uint32_t timeout,
                         pvr_txr_request_status_t *status);

/** \brief Destroy a terminal asynchronous texture request.

    An active request or one still observed by a waiter returns EBUSY. This
    function must not be called from interrupt context. Applications must
    externally serialize destroy against status queries from other threads.
*/
int pvr_txr_request_destroy(pvr_txr_request_t *request);

/** \brief Upload a linear source rectangle into an uncompressed surface.

    The source starts at the rectangle's upper-left texel. \a src_stride is in
    bytes. Linear destinations are copied row by row; twiddled destinations are
    converted in place. Twiddled YUV rectangles are rejected because their
    component packing is not equivalent to ordinary 16-bit texels. VQ surfaces
    require encoded level or byte-range uploads instead.

    \return 0 on success, or -1 with errno set.
*/
int pvr_txr_surface_upload_rect(const pvr_txr_surface_t *surface,
                                uint32_t level, uint32_t dst_x,
                                uint32_t dst_y, uint32_t width,
                                uint32_t height, const void *src,
                                size_t src_stride);

/** @} */

__END_DECLS
#endif  /* __DC_PVR_PVR_TEXTURE_H */
