#!/usr/bin/env bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Experiment 2, training: train with ACE as usual (baseline), and with ACE also
# allowed to pick deflate / lzma2 / bzip3 (--ace-extra-backends). Both get the
# same time budget.
#
# Note: Silesia is a set of unrelated files, so it is trained and evaluated on
# the same files (in-sample). PSAM is trained on 5 states and evaluated on 5
# others.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

TRAIN_SECS="${TRAIN_SECS:-600}"
THREADS="${THREADS:-6}"

# train NAME INPUT_DIR ZLI_ARGS...
train() {
    local name="$1" input="$2"
    shift 2
    mkdir -p "$OUT/trained"
    echo "[train] $name"
    "$ZLI" train "$@" "$input" -o "$OUT/trained/$name.zc" -f \
        --max-time-secs "$TRAIN_SECS" --threads "$THREADS" \
        > "$OUT/trained/$name.log" 2>&1
    grep -E "improved" "$OUT/trained/$name.log" || true
    rm -f "$OUT/trained/$name.json"
    "$ZLI" inspect "$OUT/trained/$name.zc" -o "$OUT/trained/$name.json" \
        > /dev/null
}

for variant in base ext; do
    extra=()
    if [[ "$variant" == ext ]]; then
        extra=(--ace-extra-backends)
    fi

    train "silesia_serial_$variant" "$SILESIA" -p serial "${extra[@]}"
    bench "trained_silesia_serial_$variant" "$SILESIA" \
        -c "$OUT/trained/silesia_serial_$variant.zc"

    train "sao_$variant" "$SAO_DIR" -p sao -l 7 "${extra[@]}"
    bench "trained_sao_$variant" "$SAO_DIR" \
        -c "$OUT/trained/sao_$variant.zc" -l 7

    train "psam_csv_$variant" "$PSAM_TRAIN" -p csv "${extra[@]}"
    bench "trained_psam_csv_$variant" "$PSAM_TEST" \
        -c "$OUT/trained/psam_csv_$variant.zc"
done

# Untrained baselines of the same profiles
bench untrained_sao "$SAO_DIR" -p sao -l 7

trace trained_psam_csv_ext "$PSAM_TEST" -c "$OUT/trained/psam_csv_ext.zc"
trace trained_silesia_serial_ext "$SILESIA" \
    -c "$OUT/trained/silesia_serial_ext.zc"
