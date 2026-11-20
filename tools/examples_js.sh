#!/bin/sh
# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.
#
# examples_js.sh FILE...: the Forth files given, as the JavaScript the web page loads
# (web/Makefile, examples.js): EXAMPLES.name, the lines of name.fs, the empty ones left
# out, each a string, its backslashes and quotes escaped.
echo "// Made by tools/examples_js.sh from examples/: the lines the tour types."
echo "const EXAMPLES = {"
for f in "$@"; do
   echo "  \"$(basename "$f" .fs)\": ["
   grep -v '^$' "$f" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/",/'
   echo "  ],"
done
echo "};"
