# Backend search experiments

Does letting OpenZL pick deflate (zip), LZMA2 (xz) or bzip3, next to zstd, as
the backend of its streams improve the compression ratio?

Two mechanisms are measured:
1. **No training**: `--serial-backend-search <mask>` makes
   `ZL_GRAPH_COMPRESS_GENERIC` try zstd plus the selected backends on each
   serial stream, and keep the smallest (1 = deflate, 2 = lzma2, 4 = bzip3,
   7 = all).
2. **Training**: `zli train --ace-extra-backends` lets ACE also pick these
   backends. It is compared to the same training without the flag.

## Running

```sh
make -j zli
benchmark/backend_search/fetch_data.sh   # Silesia + PSAM CSVs into ~/datasets
benchmark/backend_search/run_untrained.sh
TRAIN_SECS=600 benchmark/backend_search/run_trained.sh
benchmark/backend_search/summarize.py    # writes results/summary.{md,json}
```

`RESUME=1` skips the steps whose results already exist, to continue an
interrupted run. `ITERS` (default 1) sets the benchmark iterations; `DATASETS`, `OUT` and `ZLI`
override the paths. Timings are only meaningful on an otherwise idle machine.

Silesia is a set of unrelated files: trained compressors are evaluated on the
files they were trained on. PSAM is trained on 5 states and evaluated on 5
others.
