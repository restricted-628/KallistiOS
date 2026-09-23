# Alternative fiber provider + executor. Keep the upstream core independent.
TARGET = .build/libfiber_sh4zam.a
SOURCES = fiber fiber_sync fiber_service fiber_context fiber_switch
OBJS = $(addprefix .build/,$(addsuffix .o,$(SOURCES)))
PROBES = fiber-context-probe fiber fiber-sync fiber-math fiber-teardown \
         fiber-service-probe fiber-service-sync fiber-service-queue
ELFS = $(addprefix .build/,$(addsuffix .elf,$(PROBES)))
ADDON_ROOT := $(abspath .)

ifeq ($(wildcard $(SH4ZAM_ROOT)/include/sh4zam/shz_xmtrx.h),)
$(error Set SH4ZAM_ROOT to a SH4ZAM 0.9.0 source checkout)
endif

KOS_CFLAGS += -std=gnu17 -I$(ADDON_ROOT)/include -I$(SH4ZAM_ROOT)/include \
              -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT

.PHONY: all probes clean
.SECONDARY:
all: $(TARGET)
probes: $(ELFS)
include $(KOS_BASE)/Makefile.rules

$(TARGET): $(OBJS)
	$(KOS_AR) rcs $@ $^

.build/%.o: src/%.c
	mkdir -p $(@D)
	kos-cc -MMD -MP -c $< -o $@
.build/%.o: src/%.s
	mkdir -p $(@D)
	kos-as $< -o $@
.build/test-%.o: tests/%.c
	mkdir -p $(@D)
	kos-cc -MMD -MP -c $< -o $@

# Force one complete provider before libkallisti; never silently mix backends.
PROVIDER = -Wl,--whole-archive $(TARGET) -Wl,--no-whole-archive
.build/%.elf: .build/test-%.o $(TARGET)
	kos-cc -Wl,-Map,$@.map -o $@ $< $(EXTRA_OBJS) $(PROVIDER) $(EXTRA_LDFLAGS)
.build/fiber-service-probe.elf: EXTRA_OBJS = .build/test-fiber-service-math.o
.build/fiber-service-probe.elf: .build/test-fiber-service-math.o
.build/fiber-teardown.elf: EXTRA_LDFLAGS = -Wl,--wrap=calloc,--wrap=aligned_alloc,--wrap=free

clean:
	$(RM) $(OBJS) $(TARGET) $(ELFS) $(ELFS:=.map) .build/*.d .build/test-*.o

-include $(wildcard .build/*.d)
