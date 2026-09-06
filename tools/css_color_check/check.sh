#!/bin/sh
# Deprecated path — use `make test-css` or tools/css_parse_test/run.py
exec "$(dirname "$0")/../css_parse_test/run.py"
