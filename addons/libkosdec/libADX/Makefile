# Explicit builds; this experimental component is not in aggregate addon builds.
.DEFAULT_GOAL := sh4
SH_CC ?= kos-cc
SH_AR ?= sh-elf-ar
HOST_CC ?= cc
HOST_AR ?= ar
SHZ_INCLUDE ?= ../../libsh4zam/upstream/include
BUILD ?= .build
SH_CFLAGS ?= -O2
HOST_CFLAGS ?= -O2
SAN_CFLAGS ?= -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
STRICT = -std=gnu17 -Wall -Wextra -Werror -fno-fast-math -ffp-contract=off -fno-lto
INCLUDES = -Iinclude -isystem $(SHZ_INCLUDE)
NAMES = joint decoder adx_math adx_setup memory stream index sequence pipe service
SH_OBJS = $(BUILD)/sh4/player.o $(BUILD)/sh4/disc.o $(BUILD)/sh4/file.o $(addprefix $(BUILD)/sh4/,$(addsuffix .o,$(NAMES)))
HOST_OBJS = $(addprefix $(BUILD)/host-test/,$(addsuffix .o,$(NAMES)))
SAN_OBJS = $(addprefix $(BUILD)/sanitize/,$(addsuffix .o,$(NAMES)))
HEADERS = include/kosdec/adx_joint.h include/kosdec/adx_player.h include/kosdec/adx_service.h include/kosdec/adx_pipe.h include/kosdec/adx_sequence.h include/kosdec/adx_index.h include/kosdec/adx_stream.h include/kosdec/adx_memory.h include/kosdec/adx.h src/adx_math.h src/adx_filter.h src/adx_setup_table.h $(SHZ_INCLUDE)/sh4zam/shz_cdefs.h

.PHONY: sh4 test sanitize install clean
sh4: $(BUILD)/sh4/libADX.a
test: $(BUILD)/host-test/joint_queue $(BUILD)/host-test/joint $(BUILD)/host-test/filter $(BUILD)/host-test/initialization $(BUILD)/host-test/query $(BUILD)/host-test/file_player $(BUILD)/host-test/setup $(BUILD)/host-test/expanded $(BUILD)/host-test/seek $(BUILD)/host-test/player $(BUILD)/host-test/service $(BUILD)/host-test/pipe $(BUILD)/host-test/sequence $(BUILD)/host-test/disc $(BUILD)/host-test/rates $(BUILD)/host-test/index $(BUILD)/host-test/stream $(BUILD)/host-test/file $(BUILD)/host-test/memory $(BUILD)/host-test/framing $(BUILD)/host-test/profiles $(BUILD)/host-test/loop_metadata
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
sanitize: $(BUILD)/sanitize/joint_queue $(BUILD)/sanitize/joint $(BUILD)/sanitize/filter $(BUILD)/sanitize/initialization $(BUILD)/sanitize/query $(BUILD)/sanitize/file_player $(BUILD)/sanitize/setup $(BUILD)/sanitize/expanded $(BUILD)/sanitize/seek $(BUILD)/sanitize/player $(BUILD)/sanitize/service $(BUILD)/sanitize/pipe $(BUILD)/sanitize/sequence $(BUILD)/sanitize/disc $(BUILD)/sanitize/rates $(BUILD)/sanitize/index $(BUILD)/sanitize/stream $(BUILD)/sanitize/file $(BUILD)/sanitize/memory $(BUILD)/sanitize/framing $(BUILD)/sanitize/profiles $(BUILD)/sanitize/loop_metadata
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
	$(SH_CC) $(SH_CFLAGS) $(STRICT) -ml -m4-single $(INCLUDES) -MMD -MP -c $< -o $@
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
	install -m 644 include/kosdec/adx_joint.h include/kosdec/adx_player.h include/kosdec/adx_service.h include/kosdec/adx_pipe.h include/kosdec/adx_sequence.h include/kosdec/adx_disc.h include/kosdec/adx.h include/kosdec/adx_memory.h include/kosdec/adx_stream.h include/kosdec/adx_file.h include/kosdec/adx_index.h "$(DESTDIR)$(PREFIX)/include/kosdec/"
	install -m 644 $(BUILD)/sh4/libADX.a "$(DESTDIR)$(PREFIX)/lib/dreamcast/libADX.a"

clean:
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
