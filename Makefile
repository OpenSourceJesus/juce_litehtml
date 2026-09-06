# juce_litehtml — thin wrappers around build.py / test runners.
# CSS parse regressions: `make test-css`

PYTHON ?= python3
HEADLESS := build/headless/litehtml-headless

.PHONY: all headless test test-css test-all clean help

all: headless

help:
	@echo "make headless   build headless renderer"
	@echo "make test-css   CSS parse regression tests (needs headless)"
	@echo "make test       alias for test-css"
	@echo "make test-all   full golden suite (run_tests.py)"
	@echo "make clean      remove headless build artefacts"

headless:
	$(PYTHON) build.py headless

$(HEADLESS):
	$(PYTHON) build.py headless

test-css: $(HEADLESS)
	$(PYTHON) tools/css_parse_test/run.py

test: test-css

test-all: headless
	$(PYTHON) run_tests.py --no-build

clean:
	$(PYTHON) build.py headless --clean
