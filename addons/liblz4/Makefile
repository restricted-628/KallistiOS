# KallistiOS ##version##
#
# Bundled LZ4 1.10.0 build
# Copyright (C) 2011-2020 Yann Collet
# Copyright (C) 2026 Joseph Black
#

TARGET = liblz4.a

OBJS = lz4.o lz4frame.o lz4hc.o xxhash.o pvr_chunk_asset_lz4.o

# Keep large one-shot compression contexts off the SH-4 stack. Applications
# still pay no heap cost until they create or use a frame compression context.
KOS_CFLAGS += $(KOS_CSTD) -Wextra -DLZ4F_HEAPMODE=1

include $(KOS_BASE)/addons/Makefile.prefab
