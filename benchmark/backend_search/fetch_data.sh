#!/usr/bin/env bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Downloads the datasets used by the backend search experiments:
# - The Silesia corpus, into $DATASETS/silesia
# - Household records of the US census PUMS (PSAM) of 10 small states, as CSV.
#   5 states for training into $DATASETS/psam/train, 5 others for evaluation
#   into $DATASETS/psam/test.
set -euo pipefail

DATASETS="${DATASETS:-$HOME/datasets}"
PSAM_URL="https://www2.census.gov/programs-surveys/acs/data/pums/2023/5-Year"
PSAM_TRAIN=(hak hvt hwy hnd hsd)
PSAM_TEST=(hde hdc hri hnh hme)

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [[ ! -f "$DATASETS/silesia/dickens" ]]; then
    echo "Fetching Silesia"
    mkdir -p "$DATASETS/silesia"
    curl -fsSL -o "$tmp/silesia.zip" \
        "https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip"
    unzip -q -o "$tmp/silesia.zip" -d "$DATASETS/silesia"
fi

fetch_psam() {
    local split="$1"
    shift
    mkdir -p "$DATASETS/psam/$split"
    for state in "$@"; do
        local csv="$DATASETS/psam/$split/psam_$state.csv"
        if [[ -f "$csv" ]]; then
            continue
        fi
        echo "Fetching PSAM $state ($split)"
        curl -fsSL -o "$tmp/$state.zip" "$PSAM_URL/csv_$state.zip"
        unzip -q -o "$tmp/$state.zip" "psam_*.csv" -d "$tmp/$state"
        mv "$tmp/$state"/psam_*.csv "$csv"
    done
}
fetch_psam train "${PSAM_TRAIN[@]}"
fetch_psam test "${PSAM_TEST[@]}"

du -sh "$DATASETS/silesia" "$DATASETS/psam/train" "$DATASETS/psam/test"
