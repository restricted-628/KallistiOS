# KallistiOS ##version##
#
# arch/dreamcast/hardware/pvr/Makefile
# (c)2002 Megan Potter
#

# Memory management
OBJS := pvr_mem_core.o pvr_mem.o pvr_mem_reservation.o

# Internal functions
OBJS += pvr_buffers.o pvr_irq.o pvr_multipass_layout.o

# Init / Shutdown / Globals / Misc
OBJS += pvr_init_shutdown.o pvr_globals.o pvr_misc.o pvr_events.o

# Fast Tile Accelerator upload function
OBJS += pvr_send_to_ta.o

# Fog
OBJS += pvr_fog.o

# Palette
OBJS += pvr_palette.o

# Primitives / scene management
OBJS += pvr_prim.o pvr_scene.o pvr_geometry.o pvr_frustum.o pvr_material.o \
	pvr_chunk_model.o pvr_chunk_hierarchy.o pvr_chunk_render.o \
	pvr_chunk_binding.o pvr_lighting.o pvr_deform.o pvr_sprite_geometry.o \
	pvr_particle.o

# Texture handling
OBJS += pvr_texture.o pvr_texture_layout.o pvr_texture_reservation.o \
	pvr_texture_residency.o pvr_texture_request.o pvr_dma.o

include $(KOS_BASE)/Makefile.prefab
