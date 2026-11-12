.PHONY: all sh4 test sanitize install clean tools tools-test tools-sanitize
all: sh4
sh4 test sanitize install:
	$(MAKE) -C libADX $@
clean:
	$(MAKE) -C libADX clean
	$(MAKE) -C tools clean
tools:
	$(MAKE) -C tools all
tools-test:
	$(MAKE) -C tools test
tools-sanitize:
	$(MAKE) -C tools sanitize
