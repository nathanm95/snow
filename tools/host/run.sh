#!/bin/sh
# Builds and runs the host-side simulation harness for each screen geometry.
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/snow-harness"
mkdir -p "$OUT"
status=0
for geo in "260 260 -DPBL_ROUND" "200 228 -DPBL_RECT" "144 168 -DPBL_RECT" "180 180 -DPBL_ROUND"; do
  set -- $geo
  cc -O2 -Wall -Wno-unused-function -I. -DPBL_COLOR -DSNOW_DEBUG \
    -DPBL_DISPLAY_WIDTH=$1 -DPBL_DISPLAY_HEIGHT=$2 $3 \
    harness.c -o "$OUT/harness_$1x$2"
  "$OUT/harness_$1x$2" || status=1
done
exit $status
