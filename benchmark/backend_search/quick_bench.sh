#!/usr/bin/env bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
# Quick speed/ratio matrix used to evaluate the backend search and
# multi-threading optimizations. Prints one line per configuration:
#   name  ratio  compress-MB/s  decompress-MB/s
#
# Usage: quick_bench.sh [LABEL]   (results also appended to $OUT/quick/LABEL.txt)
# Env: ZLI, DATASETS, ITERS, THREADS (default: nproc), CONFIGS (regex filter)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

LABEL="${1:-$(date +%Y%m%d-%H%M%S)}"
THREADS="${THREADS:-$(nproc)}"
CONFIGS="${CONFIGS:-.}"
mkdir -p "$OUT/quick"
RESULT="$OUT/quick/$LABEL.txt"

# run NAME INPUT_DIR ZLI_ARGS...
run() {
    local name="$1" input="$2"
    shift 2
    [[ "$name" =~ $CONFIGS ]] || return 0
    local line
    line="$("$ZLI" benchmark "$@" -n "$ITERS" "$input" 2>&1 | tail -1)"
    # "N files: A -> B (R),  C MB/s  D MB/s"
    local ratio cspeed dspeed
    ratio="$(sed -E 's/.*\(([0-9.]+)\).*/\1/' <<<"$line")"
    cspeed="$(sed -E 's/.*\),[[:space:]]+([0-9.]+) MB\/s.*/\1/' <<<"$line")"
    dspeed="$(sed -E 's/.* ([0-9.]+) MB\/s$/\1/' <<<"$line")"
    printf "%-28s %7s %9s %9s\n" "$name" "$ratio" "$cspeed" "$dspeed" | tee -a "$RESULT"
}

echo "# $LABEL ($(git -C "$ROOT" rev-parse --short HEAD), threads=$THREADS)" | tee -a "$RESULT"
for t in 1 "$THREADS"; do
    run "silesia/generic/m1/T$t" "$SILESIA" -p generic -T "$t" --serial-backend-search 1
    run "silesia/generic/m9/T$t" "$SILESIA" -p generic -T "$t" --serial-backend-search 9
    run "silesia/generic/m13/T$t" "$SILESIA" -p generic -T "$t" --serial-backend-search 13
    run "silesia/generic/m15/T$t" "$SILESIA" -p generic -T "$t" --serial-backend-search 15
    run "psam/csv/m0/T$t" "$PSAM_TEST" -p csv -T "$t"
    run "psam/csv/m13/T$t" "$PSAM_TEST" -p csv -T "$t" --serial-backend-search 13
    run "psam/csv/m15/T$t" "$PSAM_TEST" -p csv -T "$t" --serial-backend-search 15
done
