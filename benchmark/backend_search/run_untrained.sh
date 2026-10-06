#!/usr/bin/env bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Experiment 1, no training: let ZL_GRAPH_COMPRESS_GENERIC pick the backend of
# each serial stream (--serial-backend-search), and compare with zstd only.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Masks: 0 = zstd only, 3 = zstd+deflate, 5 = zstd+lzma2, 9 = zstd+bzip3,
# 15 = all
MASKS=(${MASKS:-0 3 5 9 15})

for mask in "${MASKS[@]}"; do
    bench "untrained_silesia_generic_m$mask" "$SILESIA" \
        -p generic --serial-backend-search "$mask"
    bench "untrained_psam_csv_m$mask" "$PSAM_TEST" \
        -p csv --serial-backend-search "$mask"
done

# Reference points: a single backend on each 16 MiB chunk
for profile in zstd-plain deflate lzma2 bzip3; do
    bench "reference_silesia_$profile" "$SILESIA" -p "$profile" --chunk-size 16MiB
done
bench reference_silesia_serial "$SILESIA" -p serial

# Which backend got which bytes, when all of them are available
trace untrained_silesia_generic_m15 "$SILESIA" -p generic --serial-backend-search 15
trace untrained_psam_csv_m15 "$PSAM_TEST" -p csv --serial-backend-search 15
