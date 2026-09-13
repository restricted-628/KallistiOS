/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file dc/pvr_material_types.h
    \brief Material semantics shared by asset tools and packet preparation.
    \ingroup pvr_material
*/
#ifndef __DC_PVR_MATERIAL_TYPES_H
#define __DC_PVR_MATERIAL_TYPES_H

/** \brief Required vertex data for a compound-material step.
    \ingroup pvr_material
*/
typedef enum pvr_material_pass_role {
    PVR_MATERIAL_PASS_SURFACE = 0, /**< Ordinary surface colors and UVs. */
    PVR_MATERIAL_PASS_BUMP, /**< Black base RGB; oargb from pvr_pack_bump(). */
    PVR_MATERIAL_PASS_RESOLVE, /**< Matching coverage/depth; colors/UVs unused. */
    PVR_MATERIAL_PASS_LIGHTMAP, /**< Unlit RGB tint, alpha 255; layer UVs. */
    PVR_MATERIAL_PASS_EMISSIVE /**< Unlit RGB tint, alpha zero; layer UVs. */
} pvr_material_pass_role_t;

#endif /* __DC_PVR_MATERIAL_TYPES_H */
