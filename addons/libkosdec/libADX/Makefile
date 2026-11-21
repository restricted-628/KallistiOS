# Explicit builds; this experimental component is not in aggregate addon builds.
.DEFAULT_GOAL := sh4
SH_CC ?= kos-cc
SH_AR ?= sh-elf-ar
HOST_CC ?= cc
HOST_AR ?= ar
SHZ_INCLUDE ?= ../../libsh4zam/upstream/include
ADX_RETAINED_CLOCK ?= 0
ADX_RETAINED_GAIN ?= 0
ADX_RETAINED_START_TL ?= 0
RETAINED_PCM_INCLUDE ?= $(KOS_BASE)/kernel/arch/dreamcast/sound
RETAINED_CLOCK_FLAGS = -DMEDIA_ADX_RETAINED_CLOCK -I"$(RETAINED_PCM_INCLUDE)"
ADX_ORDINARY_STEREO ?= 0
ADX_FLYCAST_ACTIVE_PAUSE ?= 0
FLYCAST_PAUSE_INCLUDE ?= $(KOS_BASE)/kernel/arch/dreamcast/sound
ifeq ($(ADX_FLYCAST_ACTIVE_PAUSE),1)
ifneq ($(ADX_ORDINARY_STEREO),1)
$(error ADX_FLYCAST_ACTIVE_PAUSE requires ADX_ORDINARY_STEREO=1)
endif
ifneq ($(filter 1,$(ADX_RETAINED_CLOCK) $(ADX_RETAINED_GAIN) $(ADX_RETAINED_START_TL)),)
$(error ADX_FLYCAST_ACTIVE_PAUSE is incompatible with retained playback options)
endif
BUILD ?= .build/flycast-active-pause
endif
ORDINARY_PCM16_INCLUDE ?= $(KOS_BASE)/kernel/arch/dreamcast/sound
ifeq ($(ADX_ORDINARY_STEREO),1)
BUILD ?= .build/ordinary-stereo$(if $(filter 1,$(ADX_RETAINED_CLOCK)),-retained$(if $(filter 1,$(ADX_RETAINED_START_TL)),-start-tl)$(if $(filter 1,$(ADX_RETAINED_GAIN)),-gain))
$(BUILD)/sh4/player.o: PRIVATE_ORDINARY_FLAGS = -DMEDIA_ADX_ORDINARY_STEREO -I"$(ORDINARY_PCM16_INCLUDE)"
ifeq ($(ADX_FLYCAST_ACTIVE_PAUSE),1)
$(BUILD)/sh4/player.o: PRIVATE_ORDINARY_FLAGS += -DMEDIA_ADX_FLYCAST_PAUSE -I"$(FLYCAST_PAUSE_INCLUDE)"
$(BUILD)/sh4/player.o: $(FLYCAST_PAUSE_INCLUDE)/snd_flycast_pcm16_pause.h
endif
endif
ifneq ($(filter 1,$(ADX_RETAINED_GAIN) $(ADX_RETAINED_START_TL)),)
ifneq ($(ADX_RETAINED_CLOCK),1)
$(error ADX_RETAINED_GAIN and ADX_RETAINED_START_TL require ADX_RETAINED_CLOCK=1)
endif
RETAINED_GAIN_DEPS = src/adx_legacy_gain_table.h
endif
ifeq ($(ADX_RETAINED_START_TL),1)
BUILD ?= .build/retained-start-tl$(if $(filter 1,$(ADX_RETAINED_GAIN)),-gain)
RETAINED_CLOCK_FLAGS += -DMEDIA_ADX_RETAINED_START_TL
endif
ifeq ($(ADX_RETAINED_GAIN),1)
BUILD ?= .build/retained-gain
RETAINED_CLOCK_FLAGS += -DMEDIA_ADX_RETAINED_GAIN
endif
ifeq ($(ADX_RETAINED_CLOCK),1)
BUILD ?= .build/retained-clock
$(BUILD)/sh4/player.o: PRIVATE_PLAYER_FLAGS = $(RETAINED_CLOCK_FLAGS)
endif
BUILD ?= .build
SH_CFLAGS ?= -O2
HOST_CFLAGS ?= -O2
SAN_CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
STRICT = -std=gnu17 -Wall -Wextra -Werror -fno-fast-math -ffp-contract=off -fno-lto
INCLUDES = -Iinclude -isystem $(SHZ_INCLUDE)
NAMES = container compat_service compat_file sfa source loader spsd aiff au wave request partition amplitude joint joint_reader joint_sector decoder adx_math adx_setup adx_pitch memory stream index sequence pipe service
SH_OBJS = $(BUILD)/sh4/recovery.o $(BUILD)/sh4/compat_recovery.o $(BUILD)/sh4/group.o $(BUILD)/sh4/files.o $(BUILD)/sh4/named_file.o $(BUILD)/sh4/player.o $(BUILD)/sh4/disc.o $(BUILD)/sh4/file.o $(addprefix $(BUILD)/sh4/,$(addsuffix .o,$(NAMES)))
HOST_OBJS = $(addprefix $(BUILD)/host-test/,$(addsuffix .o,$(NAMES)))
SAN_OBJS = $(addprefix $(BUILD)/sanitize/,$(addsuffix .o,$(NAMES)))
HEADERS = src/adx_container_internal.h src/adx_stream_internal.h src/adx_recovery_internal.h src/adx_service_internal.h include/kosdec/adx_compat_service.h include/kosdec/adx_compat_file.h src/adx_decoder_internal.h src/adx_joint_internal.h src/adx_joint_reader_internal.h include/kosdec/adx_sfa.h include/kosdec/adx_recovery.h include/kosdec/adx_group.h include/kosdec/adx_files.h include/kosdec/adx_source.h include/kosdec/adx_named_file.h include/kosdec/adx_loader.h include/kosdec/adx_spsd.h include/kosdec/adx_aiff.h include/kosdec/adx_au.h include/kosdec/adx_partition.h include/kosdec/adx_amplitude.h include/kosdec/adx_joint_reader.h include/kosdec/adx_joint.h include/kosdec/adx_player.h include/kosdec/adx_service.h include/kosdec/adx_pipe.h include/kosdec/adx_sequence.h include/kosdec/adx_index.h include/kosdec/adx_stream.h include/kosdec/adx_memory.h include/kosdec/adx.h src/adx_math.h src/adx_pitch.h src/adx_filter.h src/adx_setup_table.h src/adx_rounded_setup_table.h $(SHZ_INCLUDE)/sh4zam/shz_cdefs.h

.PHONY: sh4 test sanitize install clean
sh4: $(BUILD)/sh4/libADX.a
test: $(BUILD)/host-test/recovery_player $(BUILD)/host-test/sfa $(BUILD)/host-test/recovery $(BUILD)/host-test/linked $(BUILD)/host-test/compat_container $(BUILD)/host-test/compat_service $(BUILD)/host-test/compat_file $(BUILD)/host-test/group $(BUILD)/host-test/files $(BUILD)/host-test/source $(BUILD)/host-test/player_error $(BUILD)/host-test/input_policy $(BUILD)/host-test/clipped_history $(BUILD)/host-test/named_file $(BUILD)/host-test/named_player $(BUILD)/host-test/pan $(BUILD)/host-test/loader $(BUILD)/host-test/start_gate $(BUILD)/host-test/spsd $(BUILD)/host-test/spsd_player $(BUILD)/host-test/wave4 $(BUILD)/host-test/aiff_player $(BUILD)/host-test/aiff $(BUILD)/host-test/au_player $(BUILD)/host-test/au $(BUILD)/host-test/wave_player $(BUILD)/host-test/wave $(BUILD)/host-test/request_joint $(BUILD)/host-test/request $(BUILD)/host-test/partition_overlay $(BUILD)/host-test/partition $(BUILD)/host-test/silence $(BUILD)/host-test/amplitude $(BUILD)/host-test/loop_count $(BUILD)/host-test/joint_player $(BUILD)/host-test/joint_reader $(BUILD)/host-test/joint_queue $(BUILD)/host-test/joint $(BUILD)/host-test/filter $(BUILD)/host-test/initialization $(BUILD)/host-test/query $(BUILD)/host-test/file_player $(BUILD)/host-test/setup $(BUILD)/host-test/expanded $(BUILD)/host-test/seek $(BUILD)/host-test/player $(BUILD)/host-test/service $(BUILD)/host-test/pipe $(BUILD)/host-test/sequence $(BUILD)/host-test/disc $(BUILD)/host-test/rates $(BUILD)/host-test/index $(BUILD)/host-test/stream $(BUILD)/host-test/file $(BUILD)/host-test/memory $(BUILD)/host-test/framing $(BUILD)/host-test/profiles $(BUILD)/host-test/loop_metadata
	$(BUILD)/host-test/sfa
	$(BUILD)/host-test/recovery_player
	$(BUILD)/host-test/recovery
	$(BUILD)/host-test/linked
	$(BUILD)/host-test/compat_container
	$(BUILD)/host-test/compat_service
	$(BUILD)/host-test/compat_file
	$(BUILD)/host-test/group
	$(BUILD)/host-test/files
	$(BUILD)/host-test/source
	$(BUILD)/host-test/player_error
	$(BUILD)/host-test/input_policy
	$(BUILD)/host-test/clipped_history
	$(BUILD)/host-test/named_file
	$(BUILD)/host-test/named_player
	$(BUILD)/host-test/pan
	$(BUILD)/host-test/loader
	$(BUILD)/host-test/start_gate
	$(BUILD)/host-test/spsd
	$(BUILD)/host-test/spsd_player
	$(BUILD)/host-test/wave4
	$(BUILD)/host-test/aiff_player
	$(BUILD)/host-test/aiff
	$(BUILD)/host-test/au_player
	$(BUILD)/host-test/au
	$(BUILD)/host-test/wave_player
	$(BUILD)/host-test/wave
	$(BUILD)/host-test/request_joint
	$(BUILD)/host-test/request
	$(BUILD)/host-test/partition_overlay
	$(BUILD)/host-test/partition
	$(BUILD)/host-test/silence
	$(BUILD)/host-test/amplitude
	$(BUILD)/host-test/loop_count
	$(BUILD)/host-test/joint_player
	$(BUILD)/host-test/joint_reader
	$(BUILD)/host-test/joint_queue
	$(BUILD)/host-test/joint
	$(BUILD)/host-test/filter
	$(BUILD)/host-test/initialization
	$(BUILD)/host-test/query
	$(BUILD)/host-test/file_player
	$(BUILD)/host-test/setup
	$(BUILD)/host-test/expanded
	$(BUILD)/host-test/seek
	$(BUILD)/host-test/player
	$(BUILD)/host-test/service
	$(BUILD)/host-test/pipe
	$(BUILD)/host-test/sequence
	$(BUILD)/host-test/disc
	$(BUILD)/host-test/rates
	$(BUILD)/host-test/index
	$(BUILD)/host-test/stream
	$(BUILD)/host-test/file
	$(BUILD)/host-test/memory
	$(BUILD)/host-test/framing
	$(BUILD)/host-test/profiles
	$(BUILD)/host-test/loop_metadata
sanitize: $(BUILD)/sanitize/recovery_player $(BUILD)/sanitize/sfa $(BUILD)/sanitize/recovery $(BUILD)/sanitize/linked $(BUILD)/sanitize/compat_container $(BUILD)/sanitize/compat_service $(BUILD)/sanitize/compat_file $(BUILD)/sanitize/group $(BUILD)/sanitize/files $(BUILD)/sanitize/source $(BUILD)/sanitize/player_error $(BUILD)/sanitize/input_policy $(BUILD)/sanitize/clipped_history $(BUILD)/sanitize/named_file $(BUILD)/sanitize/named_player $(BUILD)/sanitize/pan $(BUILD)/sanitize/loader $(BUILD)/sanitize/start_gate $(BUILD)/sanitize/spsd $(BUILD)/sanitize/spsd_player $(BUILD)/sanitize/wave4 $(BUILD)/sanitize/aiff_player $(BUILD)/sanitize/aiff $(BUILD)/sanitize/au_player $(BUILD)/sanitize/au $(BUILD)/sanitize/wave_player $(BUILD)/sanitize/wave $(BUILD)/sanitize/request_joint $(BUILD)/sanitize/request $(BUILD)/sanitize/partition_overlay $(BUILD)/sanitize/partition $(BUILD)/sanitize/silence $(BUILD)/sanitize/amplitude $(BUILD)/sanitize/loop_count $(BUILD)/sanitize/joint_player $(BUILD)/sanitize/joint_reader $(BUILD)/sanitize/joint_queue $(BUILD)/sanitize/joint $(BUILD)/sanitize/filter $(BUILD)/sanitize/initialization $(BUILD)/sanitize/query $(BUILD)/sanitize/file_player $(BUILD)/sanitize/setup $(BUILD)/sanitize/expanded $(BUILD)/sanitize/seek $(BUILD)/sanitize/player $(BUILD)/sanitize/service $(BUILD)/sanitize/pipe $(BUILD)/sanitize/sequence $(BUILD)/sanitize/disc $(BUILD)/sanitize/rates $(BUILD)/sanitize/index $(BUILD)/sanitize/stream $(BUILD)/sanitize/file $(BUILD)/sanitize/memory $(BUILD)/sanitize/framing $(BUILD)/sanitize/profiles $(BUILD)/sanitize/loop_metadata
	$(BUILD)/sanitize/sfa
	$(BUILD)/sanitize/recovery_player
	$(BUILD)/sanitize/recovery
	$(BUILD)/sanitize/linked
	$(BUILD)/sanitize/compat_container
	$(BUILD)/sanitize/compat_service
	$(BUILD)/sanitize/compat_file
	$(BUILD)/sanitize/group
	$(BUILD)/sanitize/files
	$(BUILD)/sanitize/source
	$(BUILD)/sanitize/player_error
	$(BUILD)/sanitize/input_policy
	$(BUILD)/sanitize/clipped_history
	$(BUILD)/sanitize/named_file
	$(BUILD)/sanitize/named_player
	$(BUILD)/sanitize/pan
	$(BUILD)/sanitize/loader
	$(BUILD)/sanitize/start_gate
	$(BUILD)/sanitize/spsd
	$(BUILD)/sanitize/spsd_player
	$(BUILD)/sanitize/wave4
	$(BUILD)/sanitize/aiff_player
	$(BUILD)/sanitize/aiff
	$(BUILD)/sanitize/au_player
	$(BUILD)/sanitize/au
	$(BUILD)/sanitize/wave_player
	$(BUILD)/sanitize/wave
	$(BUILD)/sanitize/request_joint
	$(BUILD)/sanitize/request
	$(BUILD)/sanitize/partition_overlay
	$(BUILD)/sanitize/partition
	$(BUILD)/sanitize/silence
	$(BUILD)/sanitize/amplitude
	$(BUILD)/sanitize/loop_count
	$(BUILD)/sanitize/joint_player
	$(BUILD)/sanitize/joint_reader
	$(BUILD)/sanitize/joint_queue
	$(BUILD)/sanitize/joint
	$(BUILD)/sanitize/filter
	$(BUILD)/sanitize/initialization
	$(BUILD)/sanitize/query
	$(BUILD)/sanitize/file_player
	$(BUILD)/sanitize/setup
	$(BUILD)/sanitize/expanded
	$(BUILD)/sanitize/seek
	$(BUILD)/sanitize/player
	$(BUILD)/sanitize/service
	$(BUILD)/sanitize/pipe
	$(BUILD)/sanitize/sequence
	$(BUILD)/sanitize/disc
	$(BUILD)/sanitize/rates
	$(BUILD)/sanitize/index
	$(BUILD)/sanitize/stream
	$(BUILD)/sanitize/file
	$(BUILD)/sanitize/memory
	$(BUILD)/sanitize/framing
	$(BUILD)/sanitize/profiles
	$(BUILD)/sanitize/loop_metadata

$(BUILD)/sh4/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(STRICT) -ml -m4-single $(INCLUDES) $(PRIVATE_PLAYER_FLAGS) $(PRIVATE_ORDINARY_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/host-test/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) $(INCLUDES) -DMEDIA_ADX_MATH_HOST_TEST -MMD -MP -c $< -o $@
$(BUILD)/sanitize/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) $(INCLUDES) -DMEDIA_ADX_MATH_HOST_TEST -MMD -MP -c $< -o $@

$(BUILD)/sh4/libADX.a: $(SH_OBJS)
	rm -f $@
	$(SH_AR) rcs $@ $^
$(BUILD)/host-test/libADX-test.a: $(HOST_OBJS)
	rm -f $@
	$(HOST_AR) rcs $@ $^
$(BUILD)/sanitize/libADX-test.a: $(SAN_OBJS)
	rm -f $@
	$(HOST_AR) rcs $@ $^
$(BUILD)/host-test/framing: tests/framing.c $(BUILD)/host-test/libADX-test.a include/kosdec/adx.h
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/framing: tests/framing.c $(BUILD)/sanitize/libADX-test.a include/kosdec/adx.h
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@
$(BUILD)/host-test/profiles: tests/profiles.c tests/profile_vectors.h $(BUILD)/host-test/libADX-test.a include/kosdec/adx.h
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/profiles: tests/profiles.c tests/profile_vectors.h $(BUILD)/sanitize/libADX-test.a include/kosdec/adx.h
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@
$(BUILD)/host-test/loop_metadata: tests/loop_metadata.c tests/profile_vectors.h $(BUILD)/host-test/libADX-test.a include/kosdec/adx.h
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/loop_metadata: tests/loop_metadata.c tests/profile_vectors.h $(BUILD)/sanitize/libADX-test.a include/kosdec/adx.h
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

# Explicit destination avoids accidentally modifying another checkout's SDK.
# Typical use: make install PREFIX="$KOS_BASE/addons" (after isolated validation).
install: sh4
	@test -n "$(PREFIX)" || { echo 'Set PREFIX to the destination addon directory'; exit 1; }
	install -d "$(DESTDIR)$(PREFIX)/include/kosdec" "$(DESTDIR)$(PREFIX)/lib/dreamcast"
	install -m 644 include/kosdec/adx_compat_service.h include/kosdec/adx_compat_file.h include/kosdec/adx_sfa.h include/kosdec/adx_recovery.h include/kosdec/adx_group.h include/kosdec/adx_files.h include/kosdec/adx_source.h include/kosdec/adx_named_file.h include/kosdec/adx_loader.h include/kosdec/adx_spsd.h include/kosdec/adx_aiff.h include/kosdec/adx_au.h include/kosdec/adx_wave.h include/kosdec/adx_request.h include/kosdec/adx_partition.h include/kosdec/adx_amplitude.h include/kosdec/adx_joint_reader.h include/kosdec/adx_joint.h include/kosdec/adx_player.h include/kosdec/adx_service.h include/kosdec/adx_pipe.h include/kosdec/adx_sequence.h include/kosdec/adx_disc.h include/kosdec/adx.h include/kosdec/adx_memory.h include/kosdec/adx_stream.h include/kosdec/adx_file.h include/kosdec/adx_index.h "$(DESTDIR)$(PREFIX)/include/kosdec/"
	install -m 644 $(BUILD)/sh4/libADX.a "$(DESTDIR)$(PREFIX)/lib/dreamcast/libADX.a"

clean:
	rm -f $(BUILD)/host-test/compat_file $(BUILD)/sanitize/compat_file
	rm -f $(ADX_NATIVE_OBJS) $(ADX_NATIVE_OBJS:.o=.d) $(BUILD)/sh4/adx_native_test.elf
	rm -f $(BUILD)/host-test/sfa $(BUILD)/sanitize/sfa $(BUILD)/host-test/recovery $(BUILD)/sanitize/recovery $(BUILD)/host-test/recovery_player $(BUILD)/sanitize/recovery_player
	rm -f $(BUILD)/host-test/linked $(BUILD)/sanitize/linked
	rm -f $(BUILD)/sanitize/files $(BUILD)/sanitize/group
	rm -f $(BUILD)/host-test/files $(BUILD)/host-test/group
	rm -f $(BUILD)/host-test/source $(BUILD)/sanitize/source
	rm -f $(BUILD)/host-test/player_error $(BUILD)/sanitize/player_error
	rm -f $(BUILD)/host-test/input_policy $(BUILD)/sanitize/input_policy
	rm -f $(BUILD)/host-test/clipped_history $(BUILD)/sanitize/clipped_history
	rm -f $(BUILD)/host-test/named_file $(BUILD)/host-test/named_file.o $(BUILD)/sanitize/named_file $(BUILD)/sanitize/named_file.o $(BUILD)/host-test/named_player $(BUILD)/sanitize/named_player
	rm -f $(BUILD)/host-test/pan $(BUILD)/sanitize/pan
	rm -f $(BUILD)/host-test/loader $(BUILD)/sanitize/loader
	rm -f $(BUILD)/host-test/start_gate $(BUILD)/sanitize/start_gate
	rm -f $(BUILD)/host-test/spsd $(BUILD)/sanitize/spsd $(BUILD)/host-test/spsd_player $(BUILD)/sanitize/spsd_player
	rm -f $(BUILD)/host-test/wave4 $(BUILD)/sanitize/wave4
	rm -f $(BUILD)/host-test/aiff_player $(BUILD)/sanitize/aiff_player
	rm -f $(BUILD)/host-test/aiff $(BUILD)/sanitize/aiff
	rm -f $(BUILD)/host-test/au_player $(BUILD)/sanitize/au_player
	rm -f $(BUILD)/host-test/au $(BUILD)/sanitize/au
	rm -f $(BUILD)/host-test/compat_container $(BUILD)/sanitize/compat_container
	rm -f $(BUILD)/host-test/wave_player $(BUILD)/sanitize/wave_player
	rm -f $(BUILD)/host-test/wave $(BUILD)/sanitize/wave
	rm -f $(BUILD)/host-test/request_joint $(BUILD)/sanitize/request_joint
	rm -f $(BUILD)/host-test/request $(BUILD)/sanitize/request
	rm -f $(BUILD)/host-test/partition_overlay $(BUILD)/sanitize/partition_overlay
	rm -f $(BUILD)/host-test/partition $(BUILD)/sanitize/partition
	rm -f $(BUILD)/host-test/silence $(BUILD)/sanitize/silence
	rm -f $(BUILD)/host-test/amplitude $(BUILD)/sanitize/amplitude
	rm -f $(BUILD)/host-test/loop_count $(BUILD)/sanitize/loop_count
	rm -f $(BUILD)/host-test/joint_player $(BUILD)/sanitize/joint_player
	rm -f $(BUILD)/host-test/joint_reader $(BUILD)/sanitize/joint_reader
	rm -f $(BUILD)/host-test/joint_queue $(BUILD)/sanitize/joint_queue
	rm -f $(BUILD)/host-test/joint $(BUILD)/sanitize/joint
	rm -f $(BUILD)/host-test/filter $(BUILD)/sanitize/filter
	rm -f $(BUILD)/host-test/initialization $(BUILD)/sanitize/initialization
	rm -f $(BUILD)/host-test/query $(BUILD)/sanitize/query
	rm -f $(BUILD)/host-test/file_player $(BUILD)/sanitize/file_player $(BUILD)/sh4/file_audio.o $(BUILD)/sh4/file_audio.su $(BUILD)/sh4/file_audio.elf
	rm -f $(BUILD)/host-test/setup $(BUILD)/sanitize/setup $(BUILD)/host-test/expanded $(BUILD)/sanitize/expanded
	rm -f $(BUILD)/host-test/seek $(BUILD)/sanitize/seek
	rm -f $(BUILD)/host-test/player $(BUILD)/sanitize/player
	rm -f $(BUILD)/host-test/service $(BUILD)/sanitize/service
	rm -f $(BUILD)/sh4/disc_audio.o $(BUILD)/sh4/disc_audio.su $(BUILD)/sh4/disc_audio.elf
	rm -f $(BUILD)/host-test/pipe $(BUILD)/sanitize/pipe
	rm -f $(BUILD)/host-test/sequence $(BUILD)/sanitize/sequence
	rm -f $(BUILD)/sh4/disc_fiber.o $(BUILD)/sh4/disc_fiber.su $(BUILD)/sh4/disc_fiber.elf
	rm -f $(BUILD)/host-test/disc $(BUILD)/sanitize/disc
	rm -f $(SH_OBJS) $(HOST_OBJS) $(SAN_OBJS) \
	    $(SH_OBJS:.o=.d) $(HOST_OBJS:.o=.d) $(SAN_OBJS:.o=.d) \
	    $(BUILD)/sh4/libADX.a $(BUILD)/host-test/libADX-test.a \
	    $(BUILD)/sanitize/libADX-test.a $(BUILD)/host-test/framing $(BUILD)/sanitize/framing \
	    $(BUILD)/host-test/profiles $(BUILD)/sanitize/profiles
	rm -f $(BUILD)/host-test/rates $(BUILD)/sanitize/rates
	rm -f $(BUILD)/host-test/index $(BUILD)/sanitize/index
	rm -f $(BUILD)/host-test/stream $(BUILD)/sanitize/stream $(BUILD)/host-test/file $(BUILD)/sanitize/file
	rm -f $(BUILD)/host-test/memory $(BUILD)/sanitize/memory
	rm -f $(BUILD)/host-test/loop_metadata $(BUILD)/sanitize/loop_metadata

-include $(SH_OBJS:.o=.d) $(HOST_OBJS:.o=.d) $(SAN_OBJS:.o=.d)

$(BUILD)/host-test/memory: tests/memory.c tests/memory_vectors.h tests/profile_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/memory: tests/memory.c tests/memory_vectors.h tests/profile_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/stream: tests/stream.c tests/stream_fixture.h tests/memory_vectors.h tests/profile_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/host-test/file: tests/file.c tests/stream_fixture.h tests/memory_vectors.h src/file.c tests/kos/fs.h include/kosdec/adx_file.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Itests -Iinclude tests/file.c src/file.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/stream: tests/stream.c tests/stream_fixture.h tests/memory_vectors.h tests/profile_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/sanitize/file: tests/file.c tests/stream_fixture.h tests/memory_vectors.h src/file.c tests/kos/fs.h include/kosdec/adx_file.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Itests -Iinclude tests/file.c src/file.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/sh4/file.o: include/kosdec/adx_file.h

$(BUILD)/host-test/index: tests/index.c tests/stream_fixture.h tests/memory_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/index: tests/index.c tests/stream_fixture.h tests/memory_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/rates: tests/rates.c tests/stream_fixture.h tests/rate_vectors.h tests/coefficient_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/rates: tests/rates.c tests/stream_fixture.h tests/rate_vectors.h tests/coefficient_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/sh4/disc.o: include/kosdec/adx_disc.h

$(BUILD)/host-test/disc: tests/disc.c tests/disc_include/dc/cdrom.h tests/stream_fixture.h tests/memory_vectors.h tests/profile_vectors.h src/disc.c include/kosdec/adx_disc.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Itests/disc_include -Iinclude tests/disc.c src/disc.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/disc: tests/disc.c tests/disc_include/dc/cdrom.h tests/stream_fixture.h tests/memory_vectors.h tests/profile_vectors.h src/disc.c include/kosdec/adx_disc.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Itests/disc_include -Iinclude tests/disc.c src/disc.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

.PHONY: retained-clock-test retained-clock-sanitize
retained-clock-test: $(BUILD)/host-test/retained_player
	$<
retained-clock-sanitize: $(BUILD)/sanitize/retained_player
	$<
$(BUILD)/host-test/retained_player: tests/retained_player.c tests/player.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a $(HEADERS) Makefile $(RETAINED_GAIN_DEPS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) $(RETAINED_CLOCK_FLAGS) -pthread -Itests/player_include -Iinclude $(filter-out tests/player.c,$(filter %.c,$^)) $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/retained_player: tests/retained_player.c tests/player.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a $(HEADERS) Makefile $(RETAINED_GAIN_DEPS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) $(RETAINED_CLOCK_FLAGS) -pthread -Itests/player_include -Iinclude $(filter-out tests/player.c,$(filter %.c,$^)) $(BUILD)/sanitize/libADX-test.a -lm -o $@

# Opt-in target numerical and source-framing fixture. It uses synthetic PCM
# oracles and exercises the linked SH-4 archive without owning an audio stream.
ADX_NATIVE_CASES = setup expanded clipped_history loop_metadata profiles sfa joint
ADX_NATIVE_OBJS = $(addprefix $(BUILD)/sh4/native-,$(addsuffix .o,$(ADX_NATIVE_CASES)))
.PHONY: native-test-elf
native-test-elf: $(BUILD)/sh4/adx_native_test.elf
$(BUILD)/sh4/native-%.o: tests/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(STRICT) -ml -m4-single $(INCLUDES) -Isrc -MMD -MP -Dmain=adx_$*_run -c $< -o $@
$(BUILD)/sh4/adx_native_test.elf: tests/native.c $(ADX_NATIVE_OBJS) $(BUILD)/sh4/libADX.a
	$(SH_CC) $(SH_CFLAGS) $(STRICT) -ml -m4-single $(INCLUDES) -o $@ $^ -lm
-include $(ADX_NATIVE_OBJS:.o=.d)

ifeq ($(ADX_FLYCAST_ACTIVE_PAUSE),1)
.PHONY: flycast-pause-test-elf flycast-pause-vsync-test-elf
flycast-pause-test-elf: $(BUILD)/sh4/adx_flycast_pause_test.elf
flycast-pause-vsync-test-elf: $(BUILD)/sh4/adx_flycast_pause_ntsc.elf $(BUILD)/sh4/adx_flycast_pause_pal.elf
$(BUILD)/sh4/adx_flycast_pause_ntsc.elf: PRIVATE_PAUSE_TEST_FLAGS = -DADX_TEST_VSYNC_MODE=1
$(BUILD)/sh4/adx_flycast_pause_pal.elf: PRIVATE_PAUSE_TEST_FLAGS = -DADX_TEST_VSYNC_MODE=2
$(BUILD)/sh4/adx_flycast_pause_test.elf $(BUILD)/sh4/adx_flycast_pause_ntsc.elf \
$(BUILD)/sh4/adx_flycast_pause_pal.elf: tests/flycast_pause.c $(BUILD)/sh4/libADX.a
	$(SH_CC) $(SH_CFLAGS) $(STRICT) $(INCLUDES) $(PRIVATE_PAUSE_TEST_FLAGS) -I"$(FLYCAST_PAUSE_INCLUDE)" -I"$(ORDINARY_PCM16_INCLUDE)" \
	    -DADX_TEST_ENABLE_ACTIVE_PAUSE=kosdec_adx_compat_player_enable_flycast_pause \
	    -Wl,--wrap=snd_flycast_pcm16_pause -o $@ $^ -lm

ADX_PAUSE_TAIL_ELFS = $(addprefix $(BUILD)/sh4/adx_flycast_pause_tail_,0.elf 1.elf 2.elf 3.elf)
.PHONY: flycast-pause-tail-test-elf
flycast-pause-tail-test-elf: $(ADX_PAUSE_TAIL_ELFS)
$(ADX_PAUSE_TAIL_ELFS): $(BUILD)/sh4/adx_flycast_pause_tail_%.elf: tests/flycast_pause_tail.c $(BUILD)/sh4/libADX.a
	$(SH_CC) $(SH_CFLAGS) $(STRICT) $(INCLUDES) -I"$(FLYCAST_PAUSE_INCLUDE)" -I"$(ORDINARY_PCM16_INCLUDE)" \
	    -DTEST_CASE=$* -Wl,--wrap=snd_flycast_pcm16_resume -o $@ $^ -lm
endif

# Optional fork-native integration exerciser. Supply your own /cd/test.adx.
# example-object is compile evidence; example-elf also needs this worker's KOS
# libraries built with exactly one fiber provider selected by the fork build.
.PHONY: example-object example-elf
example-object: $(BUILD)/sh4/disc_fiber.o
example-elf: $(BUILD)/sh4/disc_fiber.elf
$(BUILD)/sh4/disc_fiber.o: examples/disc_fiber.c include/kosdec/adx_disc.h $(HEADERS) Makefile
	mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(STRICT) -ml -m4-single $(INCLUDES) -fstack-usage -c $< -o $@
$(BUILD)/sh4/disc_fiber.elf: $(BUILD)/sh4/disc_fiber.o $(BUILD)/sh4/libADX.a
	$(SH_CC) -o $@ $^

$(BUILD)/host-test/sequence: tests/sequence.c tests/stream_fixture.h tests/profile_vectors.h tests/rate_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/sequence: tests/sequence.c tests/stream_fixture.h tests/profile_vectors.h tests/rate_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/pipe: tests/pipe.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h tests/rate_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/pipe: tests/pipe.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h tests/rate_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

# Optional retained-resource audio diagnostic; uses existing fork stream APIs.
.PHONY: audio-example
audio-example: $(BUILD)/sh4/disc_audio.elf
$(BUILD)/sh4/disc_audio.o: examples/disc_audio.c include/kosdec/adx_disc.h $(HEADERS) Makefile
	mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(STRICT) -ml -m4-single $(INCLUDES) -fstack-usage -c $< -o $@
$(BUILD)/sh4/disc_audio.elf: $(BUILD)/sh4/disc_audio.o $(BUILD)/sh4/libADX.a
	$(SH_CC) -o $@ $^

$(BUILD)/host-test/service: tests/service.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/service: tests/service.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

PLAYER_TEST_DEPS = tests/player.c tests/player_mock.c tests/player_mock.h tests/player_include/kos/fiber_service.h tests/player_include/kos/timer.h tests/player_include/dc/sound/stream.h tests/stream_fixture.h tests/profile_vectors.h src/player.c
$(BUILD)/host-test/player: $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/player.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/player: $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/player.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/seek: tests/seek.c tests/stream_fixture.h tests/profile_vectors.h tests/rate_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/seek: tests/seek.c tests/stream_fixture.h tests/profile_vectors.h tests/rate_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/expanded: tests/expanded.c tests/expanded_vectors.h tests/stream_fixture.h $(BUILD)/host-test/libADX-test.a
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/expanded: tests/expanded.c tests/expanded_vectors.h tests/stream_fixture.h $(BUILD)/sanitize/libADX-test.a
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/setup: tests/setup.c tests/setup_specials.h src/adx_setup_table.h $(BUILD)/host-test/libADX-test.a
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/setup: tests/setup.c tests/setup_specials.h src/adx_setup_table.h $(BUILD)/sanitize/libADX-test.a
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

# Optional file-music consumer; independent of direct-disc extent lookup.
.PHONY: file-audio-example
file-audio-example: $(BUILD)/sh4/file_audio.elf
$(BUILD)/sh4/file_audio.o: examples/file_audio.c include/kosdec/adx_file.h $(HEADERS) Makefile
	mkdir -p $(@D)
	$(SH_CC) $(SH_CFLAGS) $(STRICT) -ml -m4-single $(INCLUDES) -fstack-usage -c $< -o $@
$(BUILD)/sh4/file_audio.elf: $(BUILD)/sh4/file_audio.o $(BUILD)/sh4/libADX.a
	$(SH_CC) -o $@ $^

FILE_PLAYER_DEPS = tests/file_player.c tests/player_mock.c tests/player_mock.h tests/player_include/kos/fiber_service.h tests/player_include/kos/timer.h tests/player_include/dc/sound/stream.h tests/kos/fs.h tests/stream_fixture.h src/player.c src/file.c include/kosdec/adx_file.h
$(BUILD)/host-test/file_player: $(FILE_PLAYER_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Itests -Iinclude tests/file_player.c tests/player_mock.c src/player.c src/file.c $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/file_player: $(FILE_PLAYER_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Itests -Iinclude tests/file_player.c tests/player_mock.c src/player.c src/file.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/query: tests/query.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/query: tests/query.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/initialization: tests/initialization.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/initialization: tests/initialization.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/filter: tests/filter.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/filter: tests/filter.c tests/stream_fixture.h tests/profile_vectors.h tests/memory_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/joint: tests/joint.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/joint: tests/joint.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/joint_queue: tests/joint_queue.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/joint_queue: tests/joint_queue.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/joint_reader: tests/joint_reader.c tests/stream_fixture.h tests/profile_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/joint_reader: tests/joint_reader.c tests/stream_fixture.h tests/profile_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/joint_player: tests/joint_player.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/joint_player.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/joint_player: tests/joint_player.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/joint_player.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/loop_count: tests/loop_count.c tests/stream_fixture.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/loop_count: tests/loop_count.c tests/stream_fixture.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/amplitude: tests/amplitude.c tests/stream_fixture.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/amplitude: tests/amplitude.c tests/stream_fixture.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/silence: tests/silence.c tests/stream_fixture.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/silence: tests/silence.c tests/stream_fixture.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/partition: tests/partition.c tests/stream_fixture.h tests/profile_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/partition: tests/partition.c tests/stream_fixture.h tests/profile_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/partition_overlay: tests/partition_overlay.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/partition_overlay: tests/partition_overlay.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/request: tests/request.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/request: tests/request.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/request_joint: tests/request_joint.c tests/stream_fixture.h tests/profile_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/request_joint: tests/request_joint.c tests/stream_fixture.h tests/profile_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/wave: tests/wave.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/wave: tests/wave.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/wave_player: tests/wave_player.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/wave_player.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/wave_player: tests/wave_player.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/wave_player.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/au: tests/au.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/au: tests/au.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/au_player: tests/au_player.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/au_player.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/au_player: tests/au_player.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/au_player.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/aiff: tests/aiff.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/aiff: tests/aiff.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/aiff_player: tests/aiff_player.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/aiff_player.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/aiff_player: tests/aiff_player.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/aiff_player.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/wave4: tests/wave4.c tests/wave4_vectors.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/wave4: tests/wave4.c tests/wave4_vectors.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/spsd: tests/spsd.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/host-test/spsd_player: tests/spsd_player.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/spsd_player.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/spsd: tests/spsd.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/sanitize/spsd_player: tests/spsd_player.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/spsd_player.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/start_gate: tests/start_gate.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/start_gate.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/start_gate: tests/start_gate.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/start_gate.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/loader: tests/loader.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/loader: tests/loader.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/pan: tests/pan.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/pan.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/pan: tests/pan.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/pan.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

# Owned filename lifecycle; architecture and VFS models are host-only.
NAMED_DEPS = src/named_file.c include/kosdec/adx_named_file.h include/kosdec/adx_file.h tests/named_include/kos/irq.h tests/named_include/kos/thread.h tests/kos/fs.h
$(BUILD)/host-test/named_file.o: $(NAMED_DEPS) tests/named_hooks.h Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -DKOSDEC_ADX_NAMED_HOST_TEST -Itests/named_include -Itests -Iinclude -include tests/named_hooks.h -c $< -o $@
$(BUILD)/sanitize/named_file.o: $(NAMED_DEPS) tests/named_hooks.h Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -DKOSDEC_ADX_NAMED_HOST_TEST -Itests/named_include -Itests -Iinclude -include tests/named_hooks.h -c $< -o $@
$(BUILD)/host-test/named_file: tests/named_file.c tests/stream_fixture.h src/file.c $(NAMED_DEPS) $(BUILD)/host-test/named_file.o $(BUILD)/host-test/libADX-test.a
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Itests/named_include -Itests -Iinclude $< src/file.c $(BUILD)/host-test/named_file.o $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/named_file: tests/named_file.c tests/stream_fixture.h src/file.c $(NAMED_DEPS) $(BUILD)/sanitize/named_file.o $(BUILD)/sanitize/libADX-test.a
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Itests/named_include -Itests -Iinclude $< src/file.c $(BUILD)/sanitize/named_file.o $(BUILD)/sanitize/libADX-test.a -lm -o $@
$(BUILD)/host-test/named_player: $(FILE_PLAYER_DEPS) $(NAMED_DEPS) $(BUILD)/host-test/libADX-test.a
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -DKOSDEC_ADX_NAMED_HOST_TEST -DTEST_NAMED_FILE -Itests/named_include -Itests/player_include -Itests -Iinclude tests/file_player.c tests/player_mock.c src/player.c src/file.c src/named_file.c $(BUILD)/host-test/libADX-test.a -lm -o $@
$(BUILD)/sanitize/named_player: $(FILE_PLAYER_DEPS) $(NAMED_DEPS) $(BUILD)/sanitize/libADX-test.a
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -DKOSDEC_ADX_NAMED_HOST_TEST -DTEST_NAMED_FILE -Itests/named_include -Itests/player_include -Itests -Iinclude tests/file_player.c tests/player_mock.c src/player.c src/file.c src/named_file.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/clipped_history: tests/clipped_history.c src/adx_math.h $(BUILD)/host-test/libADX-test.a
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/clipped_history: tests/clipped_history.c src/adx_math.h $(BUILD)/sanitize/libADX-test.a
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude -Isrc $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/input_policy: tests/input_policy.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/input_policy: tests/input_policy.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/player_error: tests/player_error.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/player_error.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/player_error: tests/player_error.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/player_error.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/source: tests/source.c tests/stream_fixture.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude tests/source.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/source: tests/source.c tests/stream_fixture.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude tests/source.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/files: tests/files.c src/files.c src/file.c src/named_file.c tests/stream_fixture.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -DKOSDEC_ADX_NAMED_HOST_TEST -Itests/named_include -Itests -Iinclude tests/files.c src/files.c src/file.c src/named_file.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/host-test/compat_service: tests/compat_service.c src/compat_service.c include/kosdec/adx_compat_service.h
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $(filter %.c,$^) -o $@

$(BUILD)/sanitize/compat_service: tests/compat_service.c src/compat_service.c include/kosdec/adx_compat_service.h
	mkdir -p $(@D)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $(filter %.c,$^) -o $@

$(BUILD)/host-test/group: tests/group.c src/group.c $(HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Itests/player_include -Iinclude tests/group.c src/group.c -o $@

$(BUILD)/sanitize/files: tests/files.c src/files.c src/file.c src/named_file.c tests/stream_fixture.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -DKOSDEC_ADX_NAMED_HOST_TEST -Itests/named_include -Itests -Iinclude tests/files.c src/files.c src/file.c src/named_file.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/sanitize/group: tests/group.c src/group.c $(HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Itests/player_include -Iinclude tests/group.c src/group.c -o $@

$(BUILD)/host-test/linked: tests/linked.c tests/stream_fixture.h $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude tests/linked.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/linked: tests/linked.c tests/stream_fixture.h $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude tests/linked.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/recovery: tests/recovery.c src/recovery.c $(HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Itests/player_include -Iinclude tests/recovery.c src/recovery.c -o $@

$(BUILD)/sanitize/recovery: tests/recovery.c src/recovery.c $(HEADERS)
	mkdir -p $(@D)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Itests/player_include -Iinclude tests/recovery.c src/recovery.c -o $@

$(BUILD)/host-test/sfa: tests/sfa.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude tests/sfa.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/sfa: tests/sfa.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude tests/sfa.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/recovery_player: tests/recovery_player.c src/recovery.c src/compat_recovery.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/recovery_player.c tests/player_mock.c src/player.c src/recovery.c src/compat_recovery.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/recovery_player: tests/recovery_player.c src/recovery.c src/compat_recovery.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/recovery_player.c tests/player_mock.c src/player.c src/recovery.c src/compat_recovery.c $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/compat_file: tests/compat_file.c $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/compat_file: tests/compat_file.c $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -Iinclude $< $(BUILD)/sanitize/libADX-test.a -lm -o $@

$(BUILD)/host-test/compat_container: tests/compat_container.c $(PLAYER_TEST_DEPS) $(BUILD)/host-test/libADX-test.a $(HEADERS)
	$(HOST_CC) $(HOST_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/compat_container.c tests/player_mock.c src/player.c $(BUILD)/host-test/libADX-test.a -lm -o $@

$(BUILD)/sanitize/compat_container: tests/compat_container.c $(PLAYER_TEST_DEPS) $(BUILD)/sanitize/libADX-test.a $(HEADERS)
	$(HOST_CC) $(SAN_CFLAGS) $(STRICT) -pthread -Itests/player_include -Iinclude tests/compat_container.c tests/player_mock.c src/player.c $(BUILD)/sanitize/libADX-test.a -lm -o $@
