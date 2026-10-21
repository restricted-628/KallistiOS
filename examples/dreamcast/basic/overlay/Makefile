# KallistiOS ##version##
PYTHON ?= python3
OUT ?= .build
all:
	$(MAKE) -C $(KOS_BASE)/addons/libkosoverlay
	$(PYTHON) -B build.py --out $(OUT)

# Provider selection comes from the complete KOS link environment, never from
# a runtime switch. The validation report checks each provider separately.
.PHONY: all
