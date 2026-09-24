# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
PYTHON ?= python3
CC ?= cc
CFLAGS ?= -std=gnu17 -O2 -Wall -Wextra -Werror
.PHONY: all test
all: test
test:
	$(PYTHON) run.py --cc '$(CC)' --cflags '$(CFLAGS)'
