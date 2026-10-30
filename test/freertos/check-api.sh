#!/bin/sh
# Check that the FreeRTOS build defines every public function that a regular
# build does: docs/src/freertos.rst promises unsupported functions return
# UV_ENOTSUP rather than go missing. Extra symbols are not an error.
#
# Usage: check-api.sh <nm> <freertos libuv.a> <host libuv.a>
set -eu

export LC_ALL=C

symbols() {
  "$1" --defined-only -g "$2" 2>/dev/null |
    awk 'NF == 3 { print $3 }' |
    grep '^uv_[^_]' |
    sort -u
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

symbols "$1" "$2" > "$tmp/freertos"
symbols nm "$3" > "$tmp/host"

test -s "$tmp/freertos" || { echo "no symbols in $2"; exit 1; }
test -s "$tmp/host" || { echo "no symbols in $3"; exit 1; }

missing=$(comm -13 "$tmp/freertos" "$tmp/host")
if test -n "$missing"; then
  echo "missing from the FreeRTOS build:"
  echo "$missing"
  exit 1
fi

echo "ok: $(wc -l < "$tmp/host") public symbols checked"
