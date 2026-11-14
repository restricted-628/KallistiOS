CC = cc
ARM = ../../../kernel/arch/dreamcast/sound/arm
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -pedantic
SOURCES = test.c asq.c $(ARM)/aica_sequence.c
HEADERS = asq.h $(ARM)/aica_sequence.h
asq-check: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(ARM) $(SOURCES) -o $@
asq-sanitize: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(ARM) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $@

CATALOG_SOURCES = catalog-test.c catalog.c asq.c $(ARM)/aica_sequence.c
CATALOG_HEADERS = catalog.h $(HEADERS)
.PHONY: catalog-test catalog-sanitize
catalog-test: catalog-check
	./catalog-check
catalog-sanitize: catalog-asan
	./catalog-asan
catalog-check: $(CATALOG_SOURCES) $(CATALOG_HEADERS)
	$(CC) $(CFLAGS) -I$(ARM) $(CATALOG_SOURCES) -o $@
catalog-asan: $(CATALOG_SOURCES) $(CATALOG_HEADERS)
	$(CC) $(CFLAGS) -I$(ARM) -fsanitize=address,undefined -fno-omit-frame-pointer $(CATALOG_SOURCES) -o $@

WIRE_CPPFLAGS = -I$(ARM) -I../../../kernel/arch/dreamcast/include -include ../../sound-observation-test/stubs/types.h
WIRE_SOURCES = wire-test.c wire.c catalog.c asq.c $(ARM)/aica_sequence.c
.PHONY: wire-test wire-sanitize
wire-fixture.h: wire-fixture.py ../asq.py ../test_asq.py
	python3 wire-fixture.py
wire-check: $(WIRE_SOURCES) $(CATALOG_HEADERS) wire.h wire-fixture.h
	$(CC) $(CFLAGS) $(WIRE_CPPFLAGS) $(WIRE_SOURCES) -o $@
wire-asan: $(WIRE_SOURCES) $(CATALOG_HEADERS) wire.h wire-fixture.h
	$(CC) $(CFLAGS) $(WIRE_CPPFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(WIRE_SOURCES) -o $@
wire-test: wire-check
	./wire-check
wire-sanitize: wire-asan
	./wire-asan

CLIENT_SOURCES = client-test.c client.c wire.c catalog.c asq.c $(ARM)/aica_sequence.c
.PHONY: client-test client-sanitize
client-check: $(CLIENT_SOURCES) $(CATALOG_HEADERS) wire.h client.h wire-fixture.h
	$(CC) $(CFLAGS) $(WIRE_CPPFLAGS) $(CLIENT_SOURCES) -o $@
client-asan: $(CLIENT_SOURCES) $(CATALOG_HEADERS) wire.h client.h wire-fixture.h
	$(CC) $(CFLAGS) $(WIRE_CPPFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(CLIENT_SOURCES) -o $@
client-test: client-check
	./client-check
client-sanitize: client-asan
	./client-asan

.PHONY: owner-test owner-sanitize
owner-test: wire-fixture.h
	python3 owner-test.py
owner-sanitize: wire-fixture.h
	python3 owner-test.py --sanitize
