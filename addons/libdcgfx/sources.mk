# Proposed graphics-addon ownership, shared by 2D and 3D.
# This manifest is NOT included by the build yet. See README.md for gates.
# Paths are relative to KOS_BASE during the extraction transition.

DCGFX_PVR_SOURCES := \
    pvr_geometry.c pvr_frustum.c pvr_material_recipe.c \
    pvr_chunk_model.c pvr_chunk_asset.c pvr_chunk_asset_io.c \
    pvr_chunk_hierarchy.c pvr_chunk_scene.c pvr_chunk_scene_asset.c \
    pvr_chunk_render.c pvr_chunk_cache.c pvr_chunk_cache_asset.c \
    pvr_chunk_animation_asset.c pvr_chunk_morph_animation_asset.c \
    pvr_chunk_animation_catalog.c pvr_chunk_model_table.c \
    pvr_chunk_volume_asset.c pvr_chunk_resource_asset.c \
    pvr_chunk_layer_asset.c pvr_chunk_layer_binding.c pvr_chunk_layer_images.c \
    pvr_chunk_uv_asset.c pvr_chunk_resource_binding.c pvr_chunk_texture_asset.c \
    pvr_chunk_texture_binding.c pvr_chunk_route.c pvr_chunk_skin.c \
    pvr_chunk_skin_asset.c pvr_chunk_skeleton_asset.c pvr_chunk_skeleton_affine.c \
    pvr_chunk_shape.c pvr_chunk_shape_asset.c pvr_chunk_binding.c \
    pvr_lighting.c pvr_toon.c pvr_chunk_toon.c pvr_chunk_wire.c pvr_deform.c \
    pvr_sprite_geometry.c pvr_cell.c pvr_cell_asset.c pvr_tilemap.c \
    pvr_particle.c pvr_texture_residency.c

DCGFX_MATH_SOURCES := \
    matrix_stack.c matrix_compose.c matrix_camera.c animation.c collision.c
