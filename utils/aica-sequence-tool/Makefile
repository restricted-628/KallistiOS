PYTHON ?= python3
.PHONY: test sanitize
test:
	$(MAKE) -C native asq-check
	$(PYTHON) -m unittest -v test_asq.py
sanitize:
	$(MAKE) -C native asq-sanitize
	ASQ_CHECK=./native/asq-sanitize $(PYTHON) -m unittest -v test_asq.py
