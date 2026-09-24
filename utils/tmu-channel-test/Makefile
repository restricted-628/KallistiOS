# KallistiOS ##version##
# Copyright (C) 2026 Joseph Black
PYTHON ?= python3
CC ?= cc
CFLAGS ?= -O2 -std=gnu17 -Wall -Wextra -Werror
.PHONY: all test
all: test
test:
	$(PYTHON) run.py --cc '$(CC)' --cflags '$(CFLAGS)'
