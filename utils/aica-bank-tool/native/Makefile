CC = cc
PYTHON ?= python3
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wconversion -pedantic

.PHONY: test sanitize upload-test upload-sanitize
test: test-native
	$(PYTHON) differential.py ./test-native
sanitize: test-native-sanitize
	$(PYTHON) differential.py ./test-native-sanitize
test-native: abk.c abk.h test_native.c
	$(CC) $(CFLAGS) abk.c test_native.c -o $@
test-native-sanitize: abk.c abk.h test_native.c
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer abk.c test_native.c -o $@

upload-test:
	$(PYTHON) test_upload.py
upload-sanitize:
	$(PYTHON) test_upload.py --sanitize
