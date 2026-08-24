/* KallistiOS ##version##

   dc/pvr.h
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2014 Lawrence Sebald
   Copyright (C) 2023 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

   Low-level PVR 3D interface for the DC
*/

/** \file    dc/pvr.h
    \brief   Low-level PVR (3D hardware) interface.
    \ingroup pvr

    This file provides support for using the PVR 3D hardware in the Dreamcast.
    Note that this does not handle any sort of perspective transformations or
    anything of the like. This is just a very thin wrapper around the actual
    hardware support.

    This file is used for pretty much everything related to the PVR, from memory
    management to actual primitive rendering.

    \note
    This low-level header does \a not perform transformations. Applications
    that want checked, caller-owned projection into canonical or two-volume
    vertex streams can use dc/pvr_geometry.h with the matrix APIs. Checked
    material compilation and bounded visibility/clipping are provided by
    dc/pvr_material.h and dc/pvr_frustum.h, while dc/pvr_lighting.h provides
    optional caller-owned CPU vertex lighting. Admitted compact models can use
    the bounded emitter in dc/pvr_chunk_render.h. None of these interfaces
    changes PVR scene ownership.

    \author Megan Potter
    \author Roger Cattermole
    \author Paul Boese
    \author Brian Paul
    \author Lawrence Sebald
    \author Benoit Miller
    \author Ruslan Rostovtsev
    \author Joseph Black
*/

#ifndef __DC_PVR_H
#define __DC_PVR_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include <dc/memory.h>
#include <dc/sq.h>
#include <kos/img.h>
#include <kos/regfield.h>

/*  Note: This file also #includes headers from dc/pvr/. They are mostly
    at the bottom of the file to be able to use types defined throughout. */

#include "pvr/pvr_mem.h"
#include "pvr/pvr_header.h"

/** \defgroup pvr   PowerVR API
    \brief          Low-level PowerVR GPU Driver.
    \ingroup        video
*/

/* Data types ********************************************************/

/** \defgroup pvr_lists Polygon Lists
    \brief              Types pertaining to PVR list types: opaque, pt, tr, etc
    \ingroup            pvr
*/

/** \defgroup pvr_geometry Geometry
    \brief                 PVR API for managing scene geometry
    \ingroup               pvr
*/

/** \defgroup pvr_primitives Primitives
    \brief                   Polygon and sprite management
    \ingroup                 pvr_geometry
*/

/** \defgroup pvr_ctx Contexts
    \brief            User-friendly intermittent primitive representation
    \ingroup          pvr_primitives
*/

/** \defgroup pvr_mip_bias          Mipmap Bias Modes
    \brief                          Mipmap bias modes for PowerVR primitive contexts
    \ingroup                        pvr_ctx_texture

    @{
*/
typedef enum pvr_mip_bias {
    PVR_MIPBIAS_0_25   = 1,
    PVR_MIPBIAS_0_50   = 2,
    PVR_MIPBIAS_0_75   = 3,
    PVR_MIPBIAS_1_00   = 4,
    PVR_MIPBIAS_1_25   = 5,
    PVR_MIPBIAS_1_50   = 6,
    PVR_MIPBIAS_1_75   = 7,
    PVR_MIPBIAS_2_00   = 8,
    PVR_MIPBIAS_2_25   = 9,
    PVR_MIPBIAS_2_50   = 10,
    PVR_MIPBIAS_2_75   = 11,
    PVR_MIPBIAS_3_00   = 12,
    PVR_MIPBIAS_3_25   = 13,
    PVR_MIPBIAS_3_50   = 14,
    PVR_MIPBIAS_3_75   = 15,
    PVR_MIPBIAS_NORMAL = PVR_MIPBIAS_1_00    /* txr_mipmap_bias */
} pvr_mip_bias_t;
/** @} */

/** \defgroup pvr_uv_flip           U/V Flip Mode
    \brief                          Enable or disable U/V flipping on the PVR
    \ingroup                        pvr_ctx_texture

    These flags determine what happens when U/V coordinate values exceed 1.0.
    In any of the flipped cases, the specified coordinate value will flip around
    after 1.0, essentially mirroring the image. So, if you displayed an image
    with a U coordinate of 0.0 on the left hand side and 2.0 on the right hand
    side with U flipping turned on, you'd have an image that was displayed twice
    as if mirrored across the middle. This mirroring behavior happens at every
    unit boundary (so at 2.0 it returns to normal, at 3.0 it flips, etc).

    The default case is to disable mirroring. In addition, clamping of the U/V
    coordinates by PVR_UVCLAMP_U, PVR_UVCLAMP_V, or PVR_UVCLAMP_UV will disable
    the mirroring behavior.
    @{
*/
typedef enum pvr_uv_flip {
    PVR_UVFLIP_NONE, /**< No flipped coordinates */
    PVR_UVFLIP_V,    /**< Flip V only */
    PVR_UVFLIP_U,    /**< Flip U only */
    PVR_UVFLIP_UV    /**< Flip U and V */
} pvr_uv_flip_t;
/** @} */

/** \defgroup pvr_uv_clamp  U/V Clamp Mode
    \brief                  Enable or disable clamping of U/V on the PVR
    \ingroup                pvr_ctx_texture

    These flags determine whether clamping will be applied to U/V coordinate
    values that exceed 1.0. If enabled, these modes will explicitly override the
    flip/mirroring modes (PVR_UVFLIP_U, PVR_UVFLIP_V, and PVR_UVFLIP_UV), and
    will instead ensure that the coordinate(s) in question never exceed 1.0.
    @{
*/
typedef enum pvr_uv_clamp {
    PVR_UVCLAMP_NONE, /**< Disable clamping */
    PVR_UVCLAMP_V,    /**< Clamp V only */
    PVR_UVCLAMP_U,    /**< Clamp U only */
    PVR_UVCLAMP_UV    /**< Clamp U and V */
} pvr_uv_clamp_t;
/** @} */

/** \brief   PVR polygon context.
    \ingroup pvr_ctx

    You should use this more human readable format for specifying your polygon
    contexts, and then compile them into polygon headers when you are ready to
    start using them.

    This has embedded structures in it for two reasons; the first reason is to
    make it easier for me to add new stuff later without breaking existing code.
    The second reason is to make it more readable and usable.

    Unfortunately, it seems that Doxygen chokes up a little bit on this
    structure, and others like it. The documentation should still be mostly
    understandable though...

    \headerfile dc/pvr.h
*/
typedef struct {
    pvr_list_t  list_type;  /**< \brief Primitive list */
    struct {
        bool                alpha;          /**< \brief Enable alpha outside modifier */
        bool                shading;        /**< \brief Enable gourad shading */
        pvr_fog_type_t      fog_type;       /**< \brief Fog type outside modifier */
        pvr_cull_mode_t     culling;        /**< \brief Culling mode */
        bool                color_clamp;    /**< \brief Enable color clamping outside modifer */
        pvr_clip_mode_t     clip_mode;      /**< \brief Clipping mode */
        bool                modifier_mode;  /**< \brief True normal; false: cheap shadow */
        bool                specular;       /**< \brief Enable offset color outside modifier */
        bool                alpha2;         /**< \brief Enable alpha inside modifier */
        pvr_fog_type_t      fog_type2;      /**< \brief Fog type inside modifier */
        bool                color_clamp2;   /**< \brief Enable color clamping inside modifer */
    } gen;                                  /**< \brief General parameters */
    struct {
        pvr_blend_mode_t    src;            /**< \brief Source blending mode outside modifier */
        pvr_blend_mode_t    dst;            /**< \brief Dest blending mode outside modifier */
        bool                src_enable;     /**< \brief Source blending enable outside modifier */
        bool                dst_enable;     /**< \brief Dest blending enable outside modifier */
        pvr_blend_mode_t    src2;           /**< \brief Source blending mode inside modifier */
        pvr_blend_mode_t    dst2;           /**< \brief Dest blending mode inside modifier */
        bool                src_enable2;    /**< \brief Source blending mode inside modifier */
        bool                dst_enable2;    /**< \brief Dest blending mode inside modifier */
    } blend;                                /**< \brief Blending parameters */
    struct {
        pvr_color_fmts_t    color;      /**< \brief Color format in vertex */
        bool                uv;         /**< \brief True: 16-bit floating-point U/Vs; False: 32-bit */
        bool                modifier;   /**< \brief Enable modifier effects */
    } fmt;                              /**< \brief Format control */
    struct {
        pvr_depthcmp_mode_t comparison; /**< \brief Depth comparison mode */
        bool                write;      /**< \brief Enable depth writes */
    } depth;                            /**< \brief Depth comparison/write modes */
    struct {
        bool                enable;         /**< \brief Enable/disable texturing */
        pvr_filter_mode_t   filter;         /**< \brief Filtering mode */
        bool                mipmap;         /**< \brief Enable/disable mipmaps */
        pvr_mip_bias_t      mipmap_bias;    /**< \brief Mipmap bias */
        pvr_uv_flip_t       uv_flip;        /**< \brief Enable/disable U/V flipping */
        pvr_uv_clamp_t      uv_clamp;       /**< \brief Enable/disable U/V clamping */
        bool                alpha;          /**< \brief True to _disable_ texture alpha */
        pvr_txr_shading_mode_t  env;        /**< \brief Texture color contribution */
        int     width;          /**< \brief Texture width (requires a power of 2) */
        int     height;         /**< \brief Texture height (requires a power of 2) */
        int     format;         /**< \brief Texture format
                                     \see   pvr_txr_fmts */
        pvr_ptr_t base;         /**< \brief Texture pointer */
    } txr,                  /**< \brief Texturing params outside modifier */
      txr2;                 /**< \brief Texturing params inside modifier */
} pvr_poly_cxt_t;

/** \brief   PVR sprite context.
    \ingroup pvr_ctx

    You should use this more human readable format for specifying your sprite
    contexts, and then compile them into sprite headers when you are ready to
    start using them.

    Unfortunately, it seems that Doxygen chokes up a little bit on this
    structure, and others like it. The documentation should still be mostly
    understandable though...

    \headerfile dc/pvr.h
*/
typedef struct {
    pvr_list_t  list_type;  /**< \brief Primitive list */
    struct {
        bool            alpha;          /**< \brief Enable alpha */
        pvr_fog_type_t  fog_type;       /**< \brief Fog type */
        pvr_cull_mode_t culling;        /**< \brief Culling mode */
        bool            color_clamp;    /**< \brief Enable color clamp */
        pvr_clip_mode_t clip_mode;      /**< \brief Clipping mode */
        bool            specular;       /**< \brief Enable offset color */
    } gen;                              /**< \brief General parameters */
    struct {
        pvr_blend_mode_t    src;        /**< \brief Source blending mode */
        pvr_blend_mode_t    dst;        /**< \brief Dest blending mode */
        bool                src_enable; /**< \brief Source blending enable */
        bool                dst_enable; /**< \brief Dest blending enable */
    } blend;
    struct {
        pvr_depthcmp_mode_t comparison; /**< \brief Depth comparison mode */
        bool                write;      /**< \brief Enable depth writes */
    } depth;                            /**< \brief Depth comparison/write modes */
    struct {
        bool                enable;         /**< \brief Enable/disable texturing */
        pvr_filter_mode_t   filter;         /**< \brief Filtering mode */
        bool                mipmap;         /**< \brief Enable/disable mipmaps */
        pvr_mip_bias_t      mipmap_bias;    /**< \brief Mipmap bias */
        pvr_uv_flip_t       uv_flip;        /**< \brief Enable/disable U/V flipping */
        pvr_uv_clamp_t      uv_clamp;       /**< \brief Enable/disable U/V clamping */
        bool                alpha;          /**< \brief True to _disable_ texture alpha */
        pvr_txr_shading_mode_t  env;        /**< \brief Texture color contribution */
        int     width;          /**< \brief Texture width (requires a power of 2) */
        int     height;         /**< \brief Texture height (requires a power of 2) */
        int     format;         /**< \brief Texture format
                                     \see   pvr_txr_fmts */
        pvr_ptr_t base;         /**< \brief Texture pointer */
    } txr;                      /**< \brief Texturing params */
} pvr_sprite_cxt_t;

/* Constants for the above structure; thanks to Benoit Miller for these */

/** \defgroup pvr_ctx_attrib Attributes
    \brief                   PVR primitive context attributes
    \ingroup                 pvr_ctx
*/

/** \defgroup pvr_ctx_depth     Depth
    \brief                      Depth attributes for PVR polygon contexts
    \ingroup                    pvr_ctx_attrib
*/

/** \defgroup pvr_ctx_texture Texture
    \brief                    Texture attributes for PVR polygon contexts
    \ingroup                  pvr_ctx_attrib
*/

/** \defgroup pvr_ctx_color     Color
    \brief                      Color attributes for PowerVR primitive contexts
    \ingroup                    pvr_ctx_attrib
*/

/** \defgroup pvr_txr_fmts          Formats
    \brief                          PowerVR texture formats
    \ingroup                        pvr_txr_mgmt

    These are the texture formats that the PVR supports. Note that some of
    these, you can OR together with other values.

    @{
*/
#define PVR_TXRFMT_NONE         0           /**< \brief No texture */
#define PVR_TXRFMT_VQ_DISABLE   (0 << 30)   /**< \brief Not VQ encoded */
#define PVR_TXRFMT_VQ_ENABLE    (1 << 30)   /**< \brief VQ encoded */
#define PVR_TXRFMT_ARGB1555     (0 << 27)   /**< \brief 16-bit ARGB1555 */
#define PVR_TXRFMT_RGB565       (1 << 27)   /**< \brief 16-bit RGB565 */
#define PVR_TXRFMT_ARGB4444     (2 << 27)   /**< \brief 16-bit ARGB4444 */
#define PVR_TXRFMT_YUV422       (3 << 27)   /**< \brief YUV422 format */
#define PVR_TXRFMT_BUMP         (4 << 27)   /**< \brief Bumpmap format */
#define PVR_TXRFMT_PAL4BPP      (5 << 27)   /**< \brief 4BPP paletted format */
#define PVR_TXRFMT_PAL8BPP      (6 << 27)   /**< \brief 8BPP paletted format */
#define PVR_TXRFMT_TWIDDLED     (0 << 26)   /**< \brief Texture is twiddled */
#define PVR_TXRFMT_NONTWIDDLED  (1 << 26)   /**< \brief Texture is not twiddled */
#define PVR_TXRFMT_POW2_STRIDE  (0 << 25)   /**< \brief Stride is a power-of-two */
#define PVR_TXRFMT_X32_STRIDE   (1 << 25)   /**< \brief Stride is multiple of 32 */

/* Compat. */
static const uint32_t PVR_TXRFMT_NOSTRIDE   __depr("Please use PVR_TXRFMT_POW2_STRIDE.") = PVR_TXRFMT_POW2_STRIDE;
static const uint32_t PVR_TXRFMT_STRIDE     __depr("Please use PVR_TXRFMT_X32_STRIDE. Note this may cause breakage as PVR_TXRFMT_STRIDE was never working correctly." ) = PVR_TXRFMT_X32_STRIDE;

/* OR one of these into your texture format if you need it. Note that
   these coincide with the twiddled/stride bits, so you can't have a
   non-twiddled/strided texture that's paletted! */

/** \brief   8BPP palette selector

    \param  x               The palette index */
#define PVR_TXRFMT_8BPP_PAL(x)  ((x) << 25)

/** \brief   4BPP palette selector

    \param  x               The palette index */
#define PVR_TXRFMT_4BPP_PAL(x)  ((x) << 21)
/** @} */

/** \defgroup pvr_ctx_modvol        Modifier Volumes
    \brief                          PowerVR modifier volume polygon context attributes
    \ingroup                        pvr_ctx_attrib
*/

/** \defgroup pvr_mod_modes         Modes
    \brief                          Modifier volume modes for PowerVR primitive contexts
    \ingroup                        pvr_ctx_modvol

    All triangles in a single modifier volume should be of the other poly type,
    except for the last one. That should be either of the other two types,
    depending on whether you want an inclusion or exclusion volume.

    @{
*/
#define PVR_MODIFIER_OTHER_POLY         0   /**< \brief Not the last polygon in the volume */
#define PVR_MODIFIER_INCLUDE_LAST_POLY  1   /**< \brief Last polygon, inclusion volume */
#define PVR_MODIFIER_EXCLUDE_LAST_POLY  2   /**< \brief Last polygon, exclusion volume */
/** @} */

/** \defgroup pvr_primitives_headers Headers
    \brief                           Compiled headers for polygons and sprites
    \ingroup pvr_primitives

    @{
*/

/** \brief   PVR polygon header with intensity color.

    This is the equivalent of pvr_poly_hdr_t, but for use with intensity color.

    \headerfile dc/pvr.h
*/
#define pvr_poly_ic_hdr pvr_poly_hdr
typedef pvr_poly_hdr_t pvr_poly_ic_hdr_t;

/** \brief   PVR polygon header to be used with modifier volumes.

    This is the equivalent of a pvr_poly_hdr_t for use when a polygon is to be
    used with modifier volumes.

    \headerfile dc/pvr.h
*/
#define pvr_poly_mod_hdr pvr_poly_hdr
typedef pvr_poly_hdr_t pvr_poly_mod_hdr_t;

/** \brief   PVR polygon header specifically for sprites.

    This is the equivalent of a pvr_poly_hdr_t for use when a quad/sprite is to
    be rendered. Note that the color data is here, not in the vertices.

    \headerfile dc/pvr.h
*/
#define pvr_sprite_hdr pvr_poly_hdr
typedef pvr_poly_hdr_t pvr_sprite_hdr_t;

/** \brief   Modifier volume header.

    This is the header that should be submitted when dealing with setting a
    modifier volume.

    \headerfile dc/pvr.h
*/
#define pvr_mod_hdr pvr_poly_hdr
typedef pvr_poly_hdr_t pvr_mod_hdr_t;
/** @} */

/** \defgroup pvr_vertex_types  Vertices
    \brief                      PowerVR vertex types
    \ingroup                    pvr_geometry

    @{
*/

/** \brief   Generic PVR vertex type.

    The PVR chip itself supports many more vertex types, but this is the main
    one that can be used with both textured and non-textured polygons, and is
    fairly fast.

    \headerfile dc/pvr.h
*/
typedef struct pvr_vertex {
    alignas(32)
    uint32_t flags;              /**< \brief TA command (vertex flags) */
    float   x;                   /**< \brief X coordinate */
    float   y;                   /**< \brief Y coordinate */
    float   z;                   /**< \brief Z coordinate */
    union {
        struct {
            float u;             /**< \brief Texture U coordinate */
            float v;             /**< \brief Texture V coordinate */
        };
        struct {
            uint32_t argb0;      /**< \brief Vertex color when modified, outside area */
            uint32_t argb1;      /**< \brief Vertex color when modified, inside area */
        };
    };
    uint32_t argb;               /**< \brief Vertex color */
    uint32_t oargb;              /**< \brief Vertex offset color */
} pvr_vertex_t;

/** \brief   PVR vertex type: Non-textured, packed color, affected by modifier
             volume.

    This vertex type has two copies of colors. The second color is used when
    enclosed within a modifier volume.

    \headerfile dc/pvr.h
*/
typedef struct pvr_vertex_pcm {
    alignas(32)
    uint32_t flags;              /**< \brief TA command (vertex flags) */
    float   x;                   /**< \brief X coordinate */
    float   y;                   /**< \brief Y coordinate */
    float   z;                   /**< \brief Z coordinate */
    uint32_t argb0;              /**< \brief Vertex color (outside volume) */
    uint32_t argb1;              /**< \brief Vertex color (inside volume) */
    uint32_t d1;                 /**< \brief Dummy value */
    uint32_t d2;                 /**< \brief Dummy value */
} pvr_vertex_pcm_t;

/** \brief   PVR vertex type: Textured, packed color, affected by modifier volume.

    Note that this vertex type has two copies of colors, offset colors, and
    texture coords. The second set of texture coords, colors, and offset colors
    are used when enclosed within a modifier volume.

    \headerfile dc/pvr.h
*/
typedef struct pvr_vertex_tpcm {
    alignas(32)
    uint32_t flags;              /**< \brief TA command (vertex flags) */
    float   x;                   /**< \brief X coordinate */
    float   y;                   /**< \brief Y coordinate */
    float   z;                   /**< \brief Z coordinate */
    float   u0;                  /**< \brief Texture U coordinate (outside) */
    float   v0;                  /**< \brief Texture V coordinate (outside) */
    uint32_t argb0;              /**< \brief Vertex color (outside) */
    uint32_t oargb0;             /**< \brief Vertex offset color (outside) */
    float   u1;                  /**< \brief Texture U coordinate (inside) */
    float   v1;                  /**< \brief Texture V coordinate (inside) */
    uint32_t argb1;              /**< \brief Vertex color (inside) */
    uint32_t oargb1;             /**< \brief Vertex offset color (inside) */
    uint32_t d1;                 /**< \brief Dummy value */
    uint32_t d2;                 /**< \brief Dummy value */
    uint32_t d3;                 /**< \brief Dummy value */
    uint32_t d4;                 /**< \brief Dummy value */
} pvr_vertex_tpcm_t;

/** \brief   PVR vertex type: Textured sprite.

    This vertex type is to be used with the sprite polygon header and the sprite
    related commands to draw textured sprites. Note that there is no fourth Z
    coordinate. I suppose it just gets interpolated?

    The U/V coordinates in here are in the 16-bit per coordinate form. Also,
    like the fourth Z value, there is no fourth U or V, so it must get
    interpolated from the others.

    \headerfile dc/pvr.h
*/
typedef struct pvr_sprite_txr {
    alignas(32)
    uint32_t flags;               /**< \brief TA command (vertex flags) */
    float   ax;                   /**< \brief First X coordinate */
    float   ay;                   /**< \brief First Y coordinate */
    float   az;                   /**< \brief First Z coordinate */
    float   bx;                   /**< \brief Second X coordinate */
    float   by;                   /**< \brief Second Y coordinate */
    float   bz;                   /**< \brief Second Z coordinate */
    float   cx;                   /**< \brief Third X coordinate */
    float   cy;                   /**< \brief Third Y coordinate */
    float   cz;                   /**< \brief Third Z coordinate */
    float   dx;                   /**< \brief Fourth X coordinate */
    float   dy;                   /**< \brief Fourth Y coordinate */
    uint32_t dummy;               /**< \brief Dummy value */
    uint32_t auv;                 /**< \brief First U/V texture coordinates */
    uint32_t buv;                 /**< \brief Second U/V texture coordinates */
    uint32_t cuv;                 /**< \brief Third U/V texture coordinates */
} pvr_sprite_txr_t;

/** \brief   PVR vertex type: Untextured sprite.

    This vertex type is to be used with the sprite polygon header and the sprite
    related commands to draw untextured sprites (aka, quads).
*/
typedef struct pvr_sprite_col {
    alignas(32)
    uint32_t flags;              /**< \brief TA command (vertex flags) */
    float   ax;                  /**< \brief First X coordinate */
    float   ay;                  /**< \brief First Y coordinate */
    float   az;                  /**< \brief First Z coordinate */
    float   bx;                  /**< \brief Second X coordinate */
    float   by;                  /**< \brief Second Y coordinate */
    float   bz;                  /**< \brief Second Z coordinate */
    float   cx;                  /**< \brief Third X coordinate */
    float   cy;                  /**< \brief Third Y coordinate */
    float   cz;                  /**< \brief Third Z coordinate */
    float   dx;                  /**< \brief Fourth X coordinate */
    float   dy;                  /**< \brief Fourth Y coordinate */
    uint32_t d1;                 /**< \brief Dummy value */
    uint32_t d2;                 /**< \brief Dummy value */
    uint32_t d3;                 /**< \brief Dummy value */
    uint32_t d4;                 /**< \brief Dummy value */
} pvr_sprite_col_t;

/** \brief   PVR vertex type: Modifier volume.

    This vertex type is to be used with the modifier volume header to specify
    triangular modifier areas.
*/
typedef struct pvr_modifier_vol {
    alignas(32)
    uint32_t flags;              /**< \brief TA command (vertex flags) */
    float   ax;                  /**< \brief First X coordinate */
    float   ay;                  /**< \brief First Y coordinate */
    float   az;                  /**< \brief First Z coordinate */
    float   bx;                  /**< \brief Second X coordinate */
    float   by;                  /**< \brief Second Y coordinate */
    float   bz;                  /**< \brief Second Z coordinate */
    float   cx;                  /**< \brief Third X coordinate */
    float   cy;                  /**< \brief Third Y coordinate */
    float   cz;                  /**< \brief Third Z coordinate */
    uint32_t d1;                 /**< \brief Dummy value */
    uint32_t d2;                 /**< \brief Dummy value */
    uint32_t d3;                 /**< \brief Dummy value */
    uint32_t d4;                 /**< \brief Dummy value */
    uint32_t d5;                 /**< \brief Dummy value */
    uint32_t d6;                 /**< \brief Dummy value */
} pvr_modifier_vol_t;

/** @} */

/** \defgroup pvr_commands          TA Command Values
    \brief                          Command values for submitting data to the TA
    \ingroup                        pvr_primitives_headers

    These are are appropriate values for TA commands. Use whatever goes with the
    primitive type you're using.

    @{
*/
#define PVR_CMD_POLYHDR     0x80840000  /**< \brief PVR polygon header.
Striplength set to 2 */
#define PVR_CMD_VERTEX      0xe0000000  /**< \brief PVR vertex data */
#define PVR_CMD_VERTEX_EOL  0xf0000000  /**< \brief PVR vertex, end of strip */
#define PVR_CMD_USERCLIP    0x20000000  /**< \brief PVR user clipping area */
#define PVR_CMD_MODIFIER    0x80000000  /**< \brief PVR modifier volume */
#define PVR_CMD_SPRITE      0xA0000000  /**< \brief PVR sprite header */
/** @} */

/** \defgroup pvr_bitmasks          Constants and Masks
    \brief                          Polygon header constants and masks
    \ingroup                        pvr_primitives_headers

    Note that thanks to the arrangement of constants, this is mainly a matter of
    bit shifting to compile headers...

    @{
*/
#define PVR_TA_CMD_TYPE            GENMASK(26, 24)
#define PVR_TA_CMD_USERCLIP        GENMASK(17, 16)
#define PVR_TA_CMD_MODIFIER        BIT(7)
#define PVR_TA_CMD_MODIFIERMODE    BIT(6)
#define PVR_TA_CMD_CLRFMT          GENMASK(5, 4)
#define PVR_TA_CMD_TXRENABLE       BIT(3)
#define PVR_TA_CMD_SPECULAR        BIT(2)
#define PVR_TA_CMD_SHADE           BIT(1)
#define PVR_TA_CMD_UVFMT           BIT(0)
#define PVR_TA_PM1_DEPTHCMP        GENMASK(31, 29)
#define PVR_TA_PM1_CULLING         GENMASK(28, 27)
#define PVR_TA_PM1_DEPTHWRITE      BIT(26)
#define PVR_TA_PM1_TXRENABLE       BIT(25)
#define PVR_TA_PM1_MODIFIERINST    GENMASK(30, 29)
#define PVR_TA_PM2_SRCBLEND        GENMASK(31, 29)
#define PVR_TA_PM2_DSTBLEND        GENMASK(28, 26)
#define PVR_TA_PM2_SRCENABLE       BIT(25)
#define PVR_TA_PM2_DSTENABLE       BIT(24)
#define PVR_TA_PM2_FOG             GENMASK(23, 22)
#define PVR_TA_PM2_CLAMP           BIT(21)
#define PVR_TA_PM2_ALPHA           BIT(20)
#define PVR_TA_PM2_TXRALPHA        BIT(19)
#define PVR_TA_PM2_UVFLIP          GENMASK(18, 17)
#define PVR_TA_PM2_UVCLAMP         GENMASK(16, 15)
#define PVR_TA_PM2_FILTER          GENMASK(14, 13)
#define PVR_TA_PM2_SUPERSAMPLE     BIT(12)
#define PVR_TA_PM2_MIPBIAS         GENMASK(11, 8)
#define PVR_TA_PM2_TXRENV          GENMASK(7, 6)
#define PVR_TA_PM2_USIZE           GENMASK(5, 3)
#define PVR_TA_PM2_VSIZE           GENMASK(2, 0)
#define PVR_TA_PM3_MIPMAP          BIT(31)
#define PVR_TA_PM3_TXRFMT          GENMASK(30, 21)
/** @} */

/* Initialization ****************************************************/
/** \defgroup pvr_init  Initialization
    \brief              Driver initialization and shutdown
    \ingroup            pvr

    Initialization and shutdown: stuff you should only ever have to do
    once in your program.
*/

/** \defgroup pvr_binsizes          Primitive Bin Sizes
    \brief                          Available sizes for primitive bins
    \ingroup                        pvr_init
    @{
*/
#define PVR_BINSIZE_0   0   /**< \brief 0-length (disables the list) */
#define PVR_BINSIZE_8   8   /**< \brief 8-word (32-byte) length */
#define PVR_BINSIZE_16  16  /**< \brief 16-word (64-byte) length */
#define PVR_BINSIZE_32  32  /**< \brief 32-word (128-byte) length */
/** @} */

/** \brief Maximum number of hardware registration passes in one scene.
    \ingroup pvr_init
*/
#define PVR_MULTIPASS_MAX_PASSES 8u

/** \brief Configuration for one tile-accelerator registration pass.
    \ingroup pvr_init

    Each pass has an independent set of primitive bins and translucent sort
    policy. Vertex storage is shared by the complete scene because the tile
    accelerator preserves its parameter cursor between passes.
*/
typedef struct pvr_pass_config {
    /** \brief Primitive bin sizes, in the same order as pvr_init_params_t. */
    int opb_sizes[5];

    /** \brief Disable translucent polygon autosorting for this pass. */
    int autosort_disabled;
} pvr_pass_config_t;

/** \brief   PVR initialization structure
    \ingroup pvr_init

    This structure defines how the PVR initializes various parts of the system,
    including the primitive bin sizes, the vertex buffer size, and whether
    vertex DMA will be enabled.

    You essentially fill one of these in, and pass it to pvr_init().

    \headerfile dc/pvr.h
    \sa pvr_default_params
*/
typedef struct {
    /** \brief  Bin sizes.

        The bins go in the following order: opaque polygons, opaque modifiers,
        translucent polygons, translucent modifiers, punch-thrus
    */
    int     opb_sizes[5];

    /** \brief  Vertex buffer size (should be a nice round number) */
    int     vertex_buf_size;

    /** \brief  Enable vertex DMA?

        Set to non-zero if we want to enable vertex DMA mode. Note that if this
        is set, then _all_ enabled lists need to have a vertex buffer assigned,
        even if you never use that list for anything.
    */
    int     dma_enabled;

    /** \brief  Enable horizontal scaling?

        Set to non-zero if horizontal scaling is to be enabled. By enabling this
        setting and stretching your image to double the native screen width, you
        can get horizontal full-screen anti-aliasing. */
    int     fsaa_enabled;

    /** \brief  Disable translucent polygon autosort?

        Set to non-zero to disable translucent polygon autosorting. By enabling
        this setting, the PVR acts more like a traditional Z-buffered system
        when rendering translucent polygons, meaning you must pre-sort them
        yourself if you want them to appear in the right order. */
    int     autosort_disabled;


    /** \brief  OPB Overflow Count.

        Preallocates this many extra OPBs (sets of tile bins), allowing the PVR
        to use the extra space when there's too much geometry in the first OPB.

        Increasing this value can eliminate artifacts where pieces of geometry
        flicker in and out of existence along the tile boundaries. */

    int     opb_overflow_count;

    /** \brief  Disable vertex buffer double-buffering.

        Use only one single vertex buffer. This means that the PVR must finish
        rendering before the Tile Accelerator is used to prepare a new frame;
        but it allows using much smaller vertex buffers. */
    int     vbuf_doublebuf_disabled;

} pvr_init_params_t;

/** \brief   PVR initialization structure defaults
    \ingroup pvr_init

    These are the default values for pvr_init_params_t used by pvr_init_defaults().

    \sa pvr_init_defaults()
*/
static const pvr_init_params_t pvr_default_params = {
    .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
    .vertex_buf_size = 512 * 1024,
    .opb_overflow_count = 3
};

/** \brief   Initialize the PVR chip to ready status.
    \ingroup pvr_init

    This function enables the specified lists and uses the specified parameters.
    Note that bins and vertex buffers come from the texture memory pool, so only
    allocate what you actually need. Expects that a 2D mode was initialized
    already using the vid_* API.

    \param  params          The set of parameters to initialize with
    \retval 0               On success
    \retval -1              If the PVR has already been initialized or the video
                            mode active is not suitable for 3D
*/
int pvr_init(const pvr_init_params_t *params);

/** \brief Initialize the PVR for hardware multipass registration.
    \ingroup pvr_init

    Multipass registration submits several independently binned geometry
    passes to the tile accelerator, then renders their combined region array
    once. Depth and accumulated tile color are preserved across pass
    boundaries. Applications advance between passes with
    pvr_scene_next_pass().

    Both direct store-queue and buffered DMA submission are supported. Buffered
    mode requires pvr_set_pass_vertbuf_checked() for every enabled list in
    every pass. The interrupt chain preserves pass ownership and prevents
    unrelated PVR DMA from interleaving between continuation boundaries.

    Early list flushing is also supported. The first flush must occur in pass
    zero; subsequent pass boundaries run in lockstep with the TA so later-pass
    flushes retain an unambiguous hardware owner. A scene that never flushes
    early retains fully asynchronous buffered construction.

    The pass configuration is copied during initialization. No multipass
    control allocation is made by pvr_init(), so applications that keep the
    established one-pass API do not pay for this feature.

    \param  params          Common vertex, framebuffer, and overflow settings.
                            params->opb_sizes and params->autosort_disabled are
                            ignored in favor of passes.
    \param  passes         Array of pass-specific bin and sort settings.
    \param  pass_count     Number of entries in passes, from one through
                            PVR_MULTIPASS_MAX_PASSES.

    \retval 0               On success.
    \retval -1              On error, with errno set to `EINVAL`, `ENOMEM`,
                            `EOVERFLOW`, or `ENOSPC` as appropriate.
*/
int pvr_init_multipass(const pvr_init_params_t *params,
                       const pvr_pass_config_t *passes, size_t pass_count);

/** \brief   Simple PVR initialization.
    \ingroup pvr_init

    This simpler function initializes the PVR using the default parameters defined in
    pvr_default_params.

    \retval 0               On success
    \retval -1              If the PVR has already been initialized or the video
                            mode active is not suitable for 3D

    \sa pvr_default_params
*/
int pvr_init_defaults(void);

/** \brief   Shut down the PVR chip from ready status.
    \ingroup pvr_init

    This essentially leaves the video system in 2D mode as it was before the
    init.

    \retval 0               On success
    \retval -1              If the PVR has not been initialized
*/
int pvr_shutdown(void);


/* Scene rendering ***************************************************/
/** \defgroup   pvr_scene_mgmt  Scene Submission
    \brief                      PowerVR API for submitting scene geometry
    \ingroup                    pvr

    This API is used to submit triangle strips to the PVR via the TA
    interface in the chip.

    An important side note about the PVR is that all primitive types
    must be submitted grouped together. If you have 10 polygons for each
    list type, then the PVR must receive them via the TA by list type,
    with a list delimiter in between.

    So there are two modes you can use here. The first mode allows you to
    submit data directly to the TA. Your data will be forwarded to the
    chip for processing as it is fed to the PVR module. If your data
    is easily sorted into the primitive types, then this is the fastest
    mode for submitting data.

    The second mode allows you to submit data via main-RAM vertex buffers,
    which will be queued until the proper primitive type is active. In this
    case, each piece of data is copied into the vertex buffer while the
    wrong list is activated, and when the proper list becomes activated,
    the data is all sent at once. Ideally this would be via DMA, right
    now it is by store queues. This has the advantage of allowing you to
    send data in any order and have the PVR functions resolve how it should
    get sent to the hardware, but it is slower.

    The nice thing is that any combination of these modes can be used. You
    can assign a vertex buffer for any list, and it will be used to hold the
    incoming vertex data until the proper list has come up. Or if the proper
    list is already up, the data will be submitted directly. So if most of
    your polygons are opaque, and you only have a couple of translucents,
    you can set a small buffer to gather translucent data and then it will
    get sent when you do a pvr_end_scene().

    Thanks to Mikael Kalms for the idea for this API.

    \note
    Another somewhat subtle point that bears mentioning is that in the normal
    case (interrupts enabled) an interrupt handler will automatically take
    care of starting a frame rendering (after scene_finish()) and also
    flipping pages when appropriate.
*/

/** \defgroup  pvr_vertex_dma   Vertex DMA
    \brief                      Use the DMA to transfer inactive lists to the PVR
    \ingroup                    pvr_scene_mgmt
*/

/** \brief   Is vertex DMA enabled?
    \ingroup pvr_vertex_dma

    \return                 Non-zero if vertex DMA was enabled at init time
*/
int pvr_vertex_dma_enabled(void);

/** \brief   Setup a vertex buffer for one of the list types.
    \ingroup pvr_list_mgmt

    If the specified list type already has a vertex buffer, it will be replaced
    by the new one.

    \note
    Each buffer should actually be twice as long as what you will need to hold
    two frames worth of data).

    \warning
    You should generally not try to do this at any time besides before a frame
    is begun, or Bad Things May Happen.

    \param  list            The primitive list to set the buffer for.
    \param  buffer          The location of the buffer in main RAM. This must be
                            aligned to a 32-byte boundary.
    \param  len             The length of the buffer. This must be a multiple of
                            64, and must be at least 128 (even if you're not
                            using the list).

    \return                 The old buffer location (if any)
*/
void *pvr_set_vertbuf(pvr_list_t list, void *buffer, size_t len);

/** \brief   Assign a vertex buffer with checked failure reporting.
    \ingroup pvr_list_mgmt

    This is the error-reporting companion to pvr_set_vertbuf(). The complete
    allocation is split equally between the two RAM frames used by buffered
    list submission. Assignment is refused while a scene or a queued RAM frame
    could still refer to the previous buffer.

    \param  list            Enabled primitive list receiving the buffer.
    \param  buffer          32-byte-aligned main-memory allocation.
    \param  len             Allocation size, at least 128 and a multiple of 64.
    \param  old_buffer      Optional destination for the previous allocation.

    \retval 0               On success.
    \retval -1              On error, with errno set to EINVAL, ENODEV, EPERM,
                            or EBUSY.
*/
int pvr_set_vertbuf_checked(pvr_list_t list, void *buffer, size_t len,
                            void **old_buffer);

/** \brief Assign a double-buffered vertex staging allocation to one pass/list.
    \ingroup pvr_vertex_dma

    The allocation is split evenly between the two RAM frame banks, matching
    pvr_set_vertbuf_checked(). Each enabled list in a buffered multipass scene
    requires its own allocation. The application retains ownership and must not
    release or modify it while the PVR is initialized.

    Every enabled pass/list pair requires an assigned staging buffer, including
    lists that the application intends to flush before a pass boundary.

    \param pass             Zero-based registration pass.
    \param list             Primitive list within the pass.
    \param buffer           32-byte-aligned staging allocation.
    \param len              Total allocation size; at least 128 bytes and a
                            multiple of 64.
    \param old_buffer       Optional destination for the previous allocation.

    \retval 0               On success.
    \retval -1              On error, with errno set appropriately.
*/
int pvr_set_pass_vertbuf_checked(size_t pass, pvr_list_t list, void *buffer,
                                 size_t len, void **old_buffer);

/** \brief   Retrieve a pointer to the current output location in the DMA buffer
             for the requested list.
    \ingroup pvr_vertex_dma

    Vertex DMA must globally be enabled for this to work. Data may be added to
    this buffer by the user program directly; however, make sure to call
    pvr_vertbuf_written() to notify the system of any such changes.

    \param  list            The primitive list to get the buffer for.

    \return                 The tail of that list's buffer.
*/
void *pvr_vertbuf_tail(pvr_list_t list);

/** \brief   Notify the PVR system that data have been written into the output
             buffer for the given list.
    \ingroup pvr_vertex_dma

    This should always be done after writing data directly to these buffers or
    it will get overwritten by other data.

    \param  list            The primitive list that was modified.
    \param  amt             Number of bytes written. Must be a multiple of 32.
*/
void pvr_vertbuf_written(pvr_list_t list, size_t amt);

/** \brief Stable identity assigned to one PVR scene.
    \ingroup pvr_scene_mgmt

    Zero is reserved as an invalid identity. Values increase monotonically for
    the lifetime of one PVR initialization.
*/
typedef uint64_t pvr_render_id_t;

#define PVR_RENDER_ID_INVALID UINT64_C(0)

/** \brief Observable stages of one submitted render.
    \ingroup pvr_scene_mgmt
*/
typedef enum pvr_render_stage {
    PVR_RENDER_STAGE_QUEUED = 0,
    PVR_RENDER_STAGE_REGISTERED,
    PVR_RENDER_STAGE_RENDERING,
    PVR_RENDER_STAGE_COMPLETE,
    PVR_RENDER_STAGE_DISPLAYED
} pvr_render_stage_t;

/** \brief Immutable completion ticket for one submitted scene.
    \ingroup pvr_scene_mgmt

    A framebuffer ticket has target set to NULL and can reach DISPLAYED. A
    render-to-texture ticket identifies its exact destination and stops at
    COMPLETE because texture renders never enter the display page-flip queue.
*/
typedef struct pvr_render_ticket {
    pvr_render_id_t id;       /**< \brief Stable scene identity. */
    pvr_ptr_t target;         /**< \brief Texture target, or NULL for display. */
    uint32_t width;           /**< \brief Rendered width in pixels. */
    uint32_t height;          /**< \brief Rendered height in pixels. */
    uint32_t stride;          /**< \brief Target pitch in pixels. */
    uint32_t to_texture;      /**< \brief Non-zero for a texture target. */
} pvr_render_ticket_t;

/** \brief   Begin collecting data for a frame of 3D output to the off-screen
             frame buffer.
    \ingroup pvr_scene_mgmt

    You must call this function (or pvr_scene_begin_rtt()) for ever frame of
    output.
*/
void pvr_scene_begin(void);

/** \brief Finish the current registration pass and begin the next one.
    \ingroup pvr_scene_mgmt

    In direct mode, this operation closes every enabled list not already
    closed, waits for the tile accelerator to consume their end markers, and
    performs the hardware continuation sequence. In buffered mode, it closes
    the current pass in its pass-owned staging buffers; the IRQ-driven DMA
    chain performs continuation after pvr_scene_finish(). Neither path resets
    the shared parameter or overflow cursors or starts rendering early.

    The call is valid only for a scene started after pvr_init_multipass(), and
    only before the configured final pass.

    \retval 0               The next pass is ready for list submission.
    \retval -1              On error, with errno set to `ENODEV`, `ENOTSUP`,
                            `EPERM`, `EALREADY`, `ETIMEDOUT`, or `EIO`.
*/
int pvr_scene_next_pass(void);

/** \brief   Begin collecting data for a frame of 3D output to the specified
             texture.
    \ingroup pvr_scene_mgmt
    \deprecated Use pvr_scene_begin_rtt() instead.

    This function currently only supports outputting at the same size as the
    actual screen. Thus, make sure rx and ry are at least large enough for that.
    For a 640x480 output, rx will generally be 1024 on input and ry 512, as
    these are the smallest values that are powers of two and will hold the full
    screen sized output.

    \param  txr             The texture to render to.
    \param  rx              Width of the texture buffer (in pixels).
    \param  ry              Height of the texture buffer (in pixels).
*/
void pvr_scene_begin_txr(pvr_ptr_t txr, uint32_t *rx, uint32_t *ry)
    __depr("pvr_scene_begin_txr() is deprecated. Use pvr_scene_begin_rtt().");

/** \brief   Begin collecting scene data for rendering into a texture target
             with an explicit render size.
    \ingroup pvr_scene_mgmt

    This is the preferred render-to-texture API and does not require a
    screen-sized backing texture. The PVR will render into a region of render_w
    by render_h pixels, using stride_px pixels as the backing memory pitch.

    render_w and render_h describe the area to draw. stride_px describes the
    number of pixels between rows in memory and must be greater than or equal
    to render_w. For the initial 16-bit render target implementation,
    stride_px must also be a multiple of 4 pixels.

    \note Initial support is intended for 16-bit render targets, matching the
    deprecated pvr_scene_begin_txr() compatibility wrapper behavior.

    \param  txr             The texture to render to.
    \param  render_w        Width of the render area (in pixels).
    \param  render_h        Height of the render area (in pixels).
    \param  stride_px       Backing texture pitch (in pixels).

    \retval 0               On success.
    \retval -1              If the specified arguments are invalid.
*/
int pvr_scene_begin_rtt(pvr_ptr_t txr, uint32_t render_w,
                        uint32_t render_h, uint32_t stride_px);

/** \brief Inclusive pixel clipping rectangle for one scene.
    \ingroup pvr_scene_mgmt
*/
typedef struct pvr_pixel_clip {
    uint32_t left;            /**< \brief Leftmost rendered pixel. */
    uint32_t top;             /**< \brief Topmost rendered pixel. */
    uint32_t right;           /**< \brief Rightmost rendered pixel. */
    uint32_t bottom;          /**< \brief Bottommost rendered pixel. */
} pvr_pixel_clip_t;

/** \brief Set the pixel clipping rectangle for the active scene.
    \ingroup pvr_scene_mgmt

    Call this after pvr_scene_begin() or pvr_scene_begin_rtt() and before the
    scene begins TA registration. Direct submission therefore requires this
    call before the first pvr_list_begin(); buffered submission permits it until
    the first explicit list flush or scene completion.

    Coordinates are inclusive and must fit the active framebuffer or texture
    render area. Packed 24-bit framebuffer output requires even coordinates;
    invalid values are rejected rather than rounded silently.

    \param  clip            Pixel clipping rectangle.

    \retval 0               On success.
    \retval -1              On error, with errno set to EINVAL, ENODEV, EPERM,
                            or EBUSY.
*/
int pvr_scene_set_pixel_clip(const pvr_pixel_clip_t *clip);

/** \brief Get the pixel clipping rectangle configured for the active scene.
    \ingroup pvr_scene_mgmt

    \param  clip            Destination rectangle.

    \retval 0               On success.
    \retval -1              If clip is NULL, PVR is unavailable, or no scene
                            is active, with errno set appropriately.
*/
int pvr_scene_get_pixel_clip(pvr_pixel_clip_t *clip);


/** \defgroup pvr_list_mgmt Polygon Lists
    \brief                  PVR API for managing list submission
    \ingroup                pvr_scene_mgmt
*/

/** \brief Inclusive tile rectangle for a TA user-clip command.
    \ingroup pvr_list_mgmt

    One tile is 32 by 32 pixels. Hardware command fields provide six bits for
    X and four bits for Y; submission additionally checks the active render
    area.
*/
typedef struct pvr_user_clip {
    uint32_t left;            /**< \brief Leftmost tile. */
    uint32_t top;             /**< \brief Topmost tile. */
    uint32_t right;           /**< \brief Rightmost tile. */
    uint32_t bottom;          /**< \brief Bottommost tile. */
} pvr_user_clip_t;

#define PVR_USER_CLIP_MAX_X 63u /**< \brief Largest encoded user-clip X tile. */
#define PVR_USER_CLIP_MAX_Y 15u /**< \brief Largest encoded user-clip Y tile. */

/** \brief Compile a checked TA user-clip command.
    \ingroup pvr_list_mgmt

    This function is independent of scene state and is suitable for preparing
    a command in advance. pvr_user_clip_submit() adds active-target and list
    ordering checks.

    \param  command         Destination 32-byte command.
    \param  list            List affected by the command.
    \param  clip            Inclusive tile rectangle.

    \retval 0               On success.
    \retval -1              On invalid pointers, list, ordering, or hardware
                            coordinate range, with errno set to EINVAL.
*/
int pvr_user_clip_compile(pvr_poly_hdr_t *command, pvr_list_t list,
                          const pvr_user_clip_t *clip);

/** \brief Compile and submit a user-clip command to one list.
    \ingroup pvr_list_mgmt

    For a directly submitted list, that list must currently be open. A buffered
    list may receive the command directly in its RAM buffer. Do not insert a
    user-clip command between the first vertex of a strip and its end-of-strip
    vertex.

    \param  list            List affected by the command.
    \param  clip            Inclusive tile rectangle.

    \retval 0               On success.
    \retval -1              On invalid coordinates, inactive scene, disabled
                            list, direct-list ordering, flushed list, or buffer
                            exhaustion, with errno set appropriately.
*/
int pvr_user_clip_submit(pvr_list_t list, const pvr_user_clip_t *clip);

/** \brief   Begin collecting data for the given list type.
    \ingroup pvr_list_mgmt

    Lists do not have to be submitted in any particular order, but all types of
    a list must be submitted at once (unless vertex DMA mode is enabled).

    Note that there is no need to call this function in DMA mode unless you want
    to make use of pvr_prim() for compatibility. This function will
    automatically call pvr_list_finish() if a list is already opened before
    opening the new list.

    \param  list            The list to open.
    \retval 0               On success.
    \retval -1              If the list is invalid, disabled for the active
                            pass, has already been closed, or TA readiness
                            fails with `ETIMEDOUT` or `EIO`.
*/
int pvr_list_begin(pvr_list_t list);

/** \brief   End collecting data for the current list type.
    \ingroup pvr_list_mgmt

    Lists can never be opened again within a single frame once they have been
    closed. Thus submitting a primitive that belongs in a closed list is
    considered an error. Closing a list that is already closed is also an error.

    Note that if you open a list but do not submit any primitives, a blank one
    will be submitted to satisfy the hardware. If vertex DMA mode is enabled,
    then this simply sets the current list pointer to no list, and none of the
    above restrictions apply.

    \retval 0               On success.
    \retval -1              On error.
*/
int pvr_list_finish(void);

/** \brief   Submit a primitive of the current list type.
    \ingroup pvr_list_mgmt

    Note that any values submitted in this fashion will go directly to the
    hardware without any sort of buffering, and submitting a primitive of the
    wrong type will quite likely ruin your scene. Note that this also will not
    work if you haven't begun any list types (i.e., all data is queued). If DMA
    is enabled, the primitive will be appended to the end of the currently
    selected list's buffer.

    \warning
    \p data must be 32-byte aligned!

    \param  data            The primitive to submit.
    \param  size            The length of the primitive, in bytes. Must be a
                            multiple of 32.

    \retval 0               On success.
    \retval -1              On error.
*/
int pvr_prim(const void *data, size_t size);

/** \defgroup pvr_direct  Direct Rendering
    \brief                API for using direct rendering with the PVR
    \ingroup              pvr_scene_mgmt

    @{
*/

/** \cond */
extern uint32_t pvr_dr_addr;
/** \endcond */

/** \brief   Obtain the target address for Direct Rendering.

    Note that you're not expected to pass any argument. The macro can take
    arguments for compatibility reasons and will ignore them.

    \return                 A write-only destination address where a primitive
                            should be written to get ready to submit it to the
                            TA in DR mode.
*/
#define pvr_dr_target(...) __builtin_assume_aligned((void *)((pvr_dr_addr ^= 32)), 32)

/** \brief   Commit a primitive written into the Direct Rendering target address.

    \param  addr            The address returned by pvr_dr_target(), after you
                            have written the primitive to it.
*/
#define pvr_dr_commit(addr) sq_flush(addr)

/** \brief  Upload a 32-byte payload to the Tile Accelerator

    Upload the given payload to the Tile Accelerator. The difference with the
    Direct Rendering approach above is that the Store Queues are not used, and
    therefore can be used for anything else.

    \param  data            A pointer to the 32-byte payload.
                            The pointer must be aligned to 8 bytes.
*/
void pvr_send_to_ta(void *data);

/** @} */

/** \brief   Submit a primitive of the given list type.
    \ingroup pvr_list_mgmt

    Data will be queued in a vertex buffer, thus one must be available for the
    list specified (will be asserted by the code).

    \param  list            The list to submit to.
    \param  data            The primitive to submit.
    \param  size            The size of the primitive in bytes. This must be a
                            multiple of 32.

    \retval 0               On success.
    \retval -1              On error.
*/
int pvr_list_prim(pvr_list_t list, const void *data, size_t size);

/** \brief   Flush the buffered data of the given list type to the TA.
    \ingroup pvr_list_mgmt

    This completes the buffered list, appends its end marker, and transfers it
    synchronously to the TA. The list cannot be written, reopened, or flushed
    again during the current scene. pvr_scene_finish() will not transfer it a
    second time.

    This operation enables hybrid submission: selected lists can be collected
    in RAM while other lists are submitted directly. It blocks until the DMA
    engine has accepted the complete list, but it does not wait for rendering or
    display.

    In a multipass scene, early flushing must begin in pass zero. Once enabled,
    pvr_scene_next_pass() synchronously drains the remaining buffered lists in
    that pass, waits for TA acceptance, and performs continuation. This keeps
    later pass construction aligned with the hardware pass. Calling this for
    the first time after construction has advanced beyond pass zero fails with
    `ENOTSUP`.

    The scene API is not thread-safe. List construction, flushing, and scene
    completion must be serialized by the application.

    \param  list            The list to flush.

    \retval 0               On success.
    \retval -1              On error, with `errno` set to `EINVAL`, `EPERM`,
                            `ENOTSUP`, `ENODEV`, `EALREADY`, `EBUSY`, `ENOSPC`,
                            `ETIMEDOUT`, `EIO`, or an error reported by the DMA
                            layer.
*/
int pvr_list_flush(pvr_list_t list);

/** \brief   Call this after you have finished submitting all data for a frame.
    \ingroup pvr_scene_mgmt

    Once this has been called, you can not submit any more data until one of the
    pvr_scene_begin() or pvr_scene_begin_rtt() functions is called again.

    \retval 0               On success.
    \retval -1              On error (no scene started, a multipass scene has
                            not reached its final pass, or TA readiness failed
                            with `ETIMEDOUT` or `EIO`).
*/
int pvr_scene_finish(void);

/** \brief Finish a scene and return its stable completion ticket.
    \ingroup pvr_scene_mgmt

    This has the same submission behavior as pvr_scene_finish(). The ticket is
    written only after successful scene completion and remains valid until PVR
    shutdown. No object is allocated and the caller does not release it.

    \param  ticket          Destination ticket.

    \retval 0              On success.
    \retval -1             On invalid output or scene state, with errno set.
*/
int pvr_scene_finish_tracked(pvr_render_ticket_t *ticket);

/** \brief Query the latest stage reached by one render ticket.
    \ingroup pvr_scene_mgmt

    COMPLETE guarantees that the ISP/TSP no longer writes the ticket's render
    target. CPU access, DMA upload, reuse, or release of a texture target must
    not occur before that stage. COMPLETE is the highest stage returned for a
    render-to-texture ticket because that target never enters page flipping.

    \param  ticket          Ticket returned by pvr_scene_finish_tracked().
    \param  stage           Destination for the observed stage.

    \retval 0              On success.
    \retval -1             On error, with `errno` set to `EINVAL`, `ENODEV`,
                           or `ENOENT`.
*/
int pvr_render_ticket_get_stage(const pvr_render_ticket_t *ticket,
                                pvr_render_stage_t *stage);

/** \brief Wait until one render ticket reaches a requested stage.
    \ingroup pvr_scene_mgmt

    Waiting is identity-specific: completion of a different queued render does
    not satisfy the request. A zero timeout waits indefinitely; otherwise the
    timeout is in milliseconds and applies to the complete wait operation.
    DISPLAYED is rejected for render-to-texture tickets because those renders
    intentionally bypass page flipping.

    \param  ticket          Ticket returned by pvr_scene_finish_tracked().
    \param  stage           Minimum stage to reach.
    \param  timeout_ms      Zero for no deadline, otherwise milliseconds.

    \retval 0              Requested stage reached.
    \retval -1             On error, with `errno` set to `EINVAL`, `ENOTSUP`,
                           `ENODEV`, `ENOENT`, or `ETIMEDOUT`.
*/
int pvr_render_ticket_wait(const pvr_render_ticket_t *ticket,
                           pvr_render_stage_t stage,
                           unsigned int timeout_ms);

/** \brief   Block the caller until the PVR system is ready for another frame to
             be submitted.
    \ingroup pvr_scene_mgmt

    The PVR system allocates enough space for two frames: one in data collection
    mode, and another in rendering mode. If a frame is currently rendering, and
    another frame has already been closed, then the caller cannot do anything
    else until the rendering frame completes. Note also that the new frame
    cannot be activated except during a vertical blanking period, so this
    essentially waits until a rendered frame is complete and a vertical blank
    happens.

    \retval 0               On success. A new scene can be started now.
    \retval -1              On error. Something is probably very wrong...
*/
int pvr_wait_ready(void);

/** \brief   Check if the PVR system is ready for another frame to be submitted.
    \ingroup pvr_scene_mgmt

    \retval 0               If the PVR is ready for a new scene. You must call
                            pvr_wait_ready() afterwards, before starting a new
                            scene.
    \retval -1              If the PVR is not ready for a new scene yet.
*/
int pvr_check_ready(void);

/** \brief   Block the caller until the PVR has finished rendering the previous
             frame.
    \ingroup pvr_scene_mgmt

    This function can be used to wait until the PVR is done rendering a previous
    scene. This can be useful for instance to make sure that the PVR is done
    using textures that have to be updated, before updating those.

    \retval 0               On success.
    \retval -1              On error. Something is probably very wrong...
*/
int pvr_wait_render_done(void);


/* Primitive handling ************************************************/

/** \defgroup pvr_primitives_compilation Compilation
    \brief                               API for compiling primitive contexts
                                         into headers
    \ingroup pvr_ctx
*/

/** \defgroup pvr_compile_flags Extended compilation flags
    \brief                         Optional texture-header controls
    \ingroup                       pvr_primitives_compilation
    @{
*/
#define PVR_COMPILE_SUPERSAMPLE   (1u << 0) /**< Supersample the outside texture. */
#define PVR_COMPILE_SUPERSAMPLE_2 (1u << 1) /**< Supersample the inside texture. */
#define PVR_COMPILE_ALL_FLAGS     (PVR_COMPILE_SUPERSAMPLE | \
                                   PVR_COMPILE_SUPERSAMPLE_2)
/** @} */

/** \brief   Compile a polygon context into a polygon header.
    \ingroup pvr_primitives_compilation

    This function compiles a pvr_poly_cxt_t into the form needed by the hardware
    for rendering. This is for use with normal polygon headers.

    \param  dst             Where to store the compiled header.
    \param  src             The context to compile.
*/
void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src);

/** \brief   Compile a polygon context with optional header controls.
    \ingroup pvr_primitives_compilation

    This preserves pvr_poly_cxt_t's layout while exposing texture
    supersampling through the high-level compiler. The second flag affects the
    inside-volume state when a two-volume header is generated.

    \param  dst             Where to store the compiled header.
    \param  src             The context to compile.
    \param  flags           Bitwise OR of values in pvr_compile_flags.
*/
void pvr_poly_compile_ex(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src,
                         uint32_t flags);

/** \defgroup pvr_ctx_init     Initialization
    \brief                     Functions for initializing PVR polygon contexts
    \ingroup                   pvr_ctx
*/

/** \brief   Fill in a polygon context for non-textured polygons.
    \ingroup pvr_ctx_init

    This function fills in a pvr_poly_cxt_t with default parameters appropriate
    for rendering a non-textured polygon in the given list.

    \param  dst             Where to store the polygon context.
    \param  list            The primitive list to be used.
*/
void pvr_poly_cxt_col(pvr_poly_cxt_t *dst, pvr_list_t list);

/** \brief   Fill in a polygon context for a textured polygon.
    \ingroup pvr_ctx_init

    This function fills in a pvr_poly_cxt_t with default parameters appropriate
    for rendering a textured polygon in the given list.

    \param  dst             Where to store the polygon context.
    \param  list            The primitive list to be used.
    \param  textureformat   The format of the texture used.
    \param  tw              The width of the texture, in pixels.
    \param  th              The height of the texture, in pixels.
    \param  textureaddr     A pointer to the texture.
    \param  filtering       The type of filtering to use.

    \see    pvr_txr_fmts
*/
void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list,
                      int textureformat, int tw, int th, pvr_ptr_t textureaddr,
                      pvr_filter_mode_t filtering);

/** \brief   Compile a sprite context into a sprite header.
    \ingroup pvr_primitives_compilation

    This function compiles a pvr_sprite_cxt_t into the form needed by the
    hardware for rendering. This is for use with sprite headers.

    \param  dst             Where to store the compiled header.
    \param  src             The context to compile.
*/
void pvr_sprite_compile(pvr_sprite_hdr_t *dst,
                        const pvr_sprite_cxt_t *src);

/** \brief   Compile a sprite context with optional header controls.
    \ingroup pvr_primitives_compilation

    PVR_COMPILE_SUPERSAMPLE controls the sprite texture. The inside-volume
    flag is not valid for sprite headers.

    \param  dst             Where to store the compiled header.
    \param  src             The context to compile.
    \param  flags           Zero or PVR_COMPILE_SUPERSAMPLE.
*/
void pvr_sprite_compile_ex(pvr_sprite_hdr_t *dst,
                           const pvr_sprite_cxt_t *src, uint32_t flags);

/** \brief   Fill in a sprite context for non-textured sprites.
    \ingroup pvr_ctx_init

    This function fills in a pvr_sprite_cxt_t with default parameters
    appropriate for rendering a non-textured sprite in the given list.

    \param  dst             Where to store the sprite context.
    \param  list            The primitive list to be used.
*/
void pvr_sprite_cxt_col(pvr_sprite_cxt_t *dst, pvr_list_t list);

/** \brief   Fill in a sprite context for a textured sprite.
    \ingroup pvr_ctx_init

    This function fills in a pvr_sprite_cxt_t with default parameters
    appropriate for rendering a textured sprite in the given list.

    \param  dst             Where to store the sprite context.
    \param  list            The primitive list to be used.
    \param  textureformat   The format of the texture used.
    \param  tw              The width of the texture, in pixels.
    \param  th              The height of the texture, in pixels.
    \param  textureaddr     A pointer to the texture.
    \param  filtering       The type of filtering to use.

    \see    pvr_txr_fmts
*/
void pvr_sprite_cxt_txr(pvr_sprite_cxt_t *dst, pvr_list_t list,
                        int textureformat, int tw, int th, pvr_ptr_t textureaddr,
                        pvr_filter_mode_t filtering);

/** \brief   Create a modifier volume header.
    \ingroup pvr_primitives_compilation

    This function fills in a modifier volume header with the parameters
    specified. Note that unlike for polygons and sprites, there is no context
    step for modifiers.

    \param  dst             Where to store the modifier header.
    \param  list            The primitive list to be used.
    \param  mode            The mode for this modifier.
    \param  cull            The culling mode to use.

    \see    pvr_mod_modes
    \see    pvr_cull_modes
*/
void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list, uint32_t mode,
                     uint32_t cull);

/** \brief   Compile a polygon context into a polygon header that is affected by
             modifier volumes.
    \ingroup pvr_primitives_compilation

    This function works pretty similarly to pvr_poly_compile(), but compiles
    into the header type that is affected by a modifier volume. The context
    should have been created with either pvr_poly_cxt_col_mod() or
    pvr_poly_cxt_txr_mod().

    \param  dst             Where to store the compiled header.
    \param  src             The context to compile.
*/
void pvr_poly_mod_compile(pvr_poly_mod_hdr_t *dst, const pvr_poly_cxt_t *src);

/** \brief   Compile a two-volume polygon context with optional controls.
    \ingroup pvr_primitives_compilation

    PVR_COMPILE_SUPERSAMPLE controls the outside-volume texture and
    PVR_COMPILE_SUPERSAMPLE_2 controls the inside-volume texture.

    \param  dst             Where to store the compiled header.
    \param  src             The context to compile.
    \param  flags           Bitwise OR of values in pvr_compile_flags.
*/
void pvr_poly_mod_compile_ex(pvr_poly_mod_hdr_t *dst,
                             const pvr_poly_cxt_t *src, uint32_t flags);

/** \brief   Fill in a polygon context for non-textured polygons affected by a
             modifier volume.
    \ingroup pvr_ctx_init

    This function fills in a pvr_poly_cxt_t with default parameters appropriate
    for rendering a non-textured polygon in the given list that will be affected
    by modifier volumes.

    \param  dst             Where to store the polygon context.
    \param  list            The primitive list to be used.
*/
void pvr_poly_cxt_col_mod(pvr_poly_cxt_t *dst, pvr_list_t list);

/** \brief   Fill in a polygon context for a textured polygon affected by
             modifier volumes.
    \ingroup pvr_ctx_init

    This function fills in a pvr_poly_cxt_t with default parameters appropriate
    for rendering a textured polygon in the given list and being affected by
    modifier volumes.

    \param  dst             Where to store the polygon context.
    \param  list            The primitive list to be used.
    \param  textureformat   The format of the texture used (outside).
    \param  tw              The width of the texture, in pixels (outside).
    \param  th              The height of the texture, in pixels (outside).
    \param  textureaddr     A pointer to the texture (outside).
    \param  filtering       The type of filtering to use (outside).
    \param  textureformat2  The format of the texture used (inside).
    \param  tw2             The width of the texture, in pixels (inside).
    \param  th2             The height of the texture, in pixels (inside).
    \param  textureaddr2    A pointer to the texture (inside).
    \param  filtering2      The type of filtering to use (inside).

    \see    pvr_txr_fmts
*/
void pvr_poly_cxt_txr_mod(pvr_poly_cxt_t *dst, pvr_list_t list,
                          int textureformat, int tw, int th,
                          pvr_ptr_t textureaddr, pvr_filter_mode_t filtering,
                          int textureformat2, int tw2, int th2,
                          pvr_ptr_t textureaddr2, pvr_filter_mode_t filtering2);

/** \brief   Get a pointer to the front buffer.
    \ingroup pvr_txr_mgmt

    This function can be used to retrieve a pointer to the front buffer, aka.
    the last fully rendered buffer that is either being displayed right now,
    or is queued to be displayed.

    Note that the frame buffers lie in 32-bit memory, while textures lie in
    64-bit memory. The address returned will point to 64-bit memory, but the
    front buffer cannot be used directly as a regular texture.

    \return                 A pointer to the front buffer.
*/
pvr_ptr_t pvr_get_front_buffer(void);

/** \brief   Get a pointer to the back buffer.
    \ingroup pvr_txr_mgmt

    This function can be used to retrieve a pointer to the back buffer, aka.
    the frame buffer that will be rendered to.

    Note that the frame buffers lie in 32-bit memory, while textures lie in
    64-bit memory. The address returned will point to 64-bit memory, but the
    back buffer cannot be used directly as a regular texture.

    \return                 A pointer to the back buffer.
*/
pvr_ptr_t pvr_get_back_buffer(void);

/*********************************************************************/

#include "pvr/pvr_regs.h"
#include "pvr/pvr_misc.h"
#include "pvr/pvr_dma.h"
#include "pvr/pvr_fog.h"
#include "pvr/pvr_pal.h"
#include "pvr/pvr_txr.h"
#include "pvr/pvr_legacy.h"

__END_DECLS

#endif  /* __DC_PVR_H */
