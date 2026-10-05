# Copyright (c) Meta Platforms, Inc. and affiliates.
# Shared settings of the backend search experiments. Meant to be sourced.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ZLI="${ZLI:-$ROOT/zli}"
DATASETS="${DATASETS:-$HOME/datasets}"
OUT="${OUT:-$ROOT/benchmark/backend_search/results}"
ITERS="${ITERS:-1}"
# With RESUME=1, steps whose results already exist are skipped
RESUME="${RESUME:-0}"

SILESIA="$DATASETS/silesia"
SAO_DIR="$DATASETS/silesia_sao" # the sao profile only applies to the sao file
PSAM_TRAIN="$DATASETS/psam/train"
PSAM_TEST="$DATASETS/psam/test"

mkdir -p "$OUT"
if [[ ! -e "$SAO_DIR/sao" ]]; then
    mkdir -p "$SAO_DIR"
    ln -s "$SILESIA/sao" "$SAO_DIR/sao"
fi

# bench NAME INPUT_DIR ZLI_ARGS...
# Benchmarks INPUT_DIR, writing the per file CSV to $OUT/bench/NAME.csv
bench() {
    local name="$1" input="$2"
    shift 2
    mkdir -p "$OUT/bench"
    if [[ "$RESUME" == 1 && -s "$OUT/bench/$name.csv" ]]; then
        echo "[bench] $name: already done"
        return
    fi
    echo "[bench] $name"
    "$ZLI" benchmark "$@" -n "$ITERS" --output-csv "$OUT/bench/$name.csv" \
        "$input" > "$OUT/bench/$name.log" 2>&1
    tail -1 "$OUT/bench/$name.log"
}

# trace NAME INPUT_DIR ZLI_ARGS...
# Compresses every file of INPUT_DIR, and records a decompression trace of each
# into $OUT/traces/NAME/, to break down which backend got which bytes.
trace() {
    local name="$1" input="$2"
    shift 2
    local dir="$OUT/traces/$name"
    local tmp
    tmp="$(mktemp -d)"
    mkdir -p "$dir"
    echo "[trace] $name"
    for f in "$input"/*; do
        local base
        base="$(basename "$f")"
        if [[ "$RESUME" == 1 && -s "$dir/$base.cbor" ]]; then
            continue
        fi
        "$ZLI" compress "$@" "$f" -o "$tmp/$base.zl" -f > /dev/null 2>&1
        "$ZLI" decompress "$tmp/$base.zl" -o "$tmp/$base" -f \
            --trace "$dir/$base.cbor" --no-stream-preview > /dev/null 2>&1
        cmp -s "$f" "$tmp/$base" || echo "ROUND TRIP FAILURE: $name $base"
        rm -f "$tmp/$base.zl" "$tmp/$base"
    done
    rm -rf "$tmp"
}
