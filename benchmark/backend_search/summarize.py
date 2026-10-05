#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
"""
Summarizes the results of run_untrained.sh and run_trained.sh into
results/summary.json and results/summary.md. Standard library only.
"""

import argparse
import csv
import json
import re
import struct
from collections import defaultdict
from pathlib import Path

BACKENDS = ["zstd", "deflate", "lzma2", "bzip3", "lz4"]


def load_bench(path: Path) -> dict:
    src = comp = 0
    ctime = dtime = 0.0
    files = 0
    with open(path) as f:
        for row in csv.DictReader(f):
            src += int(row["srcSize"])
            comp += int(row["compressedSize"])
            ctime += float(row["ctimeMs"])
            dtime += float(row["dtimeMs"])
            files += 1
    return {
        "files": files,
        "srcSize": src,
        "compressedSize": comp,
        "ratio": src / comp if comp else None,
        "cMBps": src / 1e3 / ctime if ctime else None,
        "dMBps": src / 1e3 / dtime if dtime else None,
    }


def cbor_decode(buf: bytes, i: int = 0):
    """Minimal CBOR decoder, enough for zli traces."""
    ib = buf[i]
    major, info = ib >> 5, ib & 31
    i += 1
    if major == 7:
        if info == 25:
            return 0.0, i + 2  # half floats are not used by the traces
        if info == 26:
            return struct.unpack(">f", buf[i : i + 4])[0], i + 4
        if info == 27:
            return struct.unpack(">d", buf[i : i + 8])[0], i + 8
        return {20: False, 21: True, 22: None}.get(info), i
    if info < 24:
        value = info
    elif info == 24:
        value, i = buf[i], i + 1
    elif info == 25:
        value, i = struct.unpack(">H", buf[i : i + 2])[0], i + 2
    elif info == 26:
        value, i = struct.unpack(">I", buf[i : i + 4])[0], i + 4
    elif info == 27:
        value, i = struct.unpack(">Q", buf[i : i + 8])[0], i + 8
    else:
        raise ValueError(f"Unsupported CBOR additional info {info}")
    if major == 0:
        return value, i
    if major == 1:
        return -1 - value, i
    if major == 2:
        return buf[i : i + value], i + value
    if major == 3:
        return buf[i : i + value].decode("utf-8", "replace"), i + value
    if major == 4:
        out = []
        for _ in range(value):
            item, i = cbor_decode(buf, i)
            out.append(item)
        return out, i
    if major == 5:
        out = {}
        for _ in range(value):
            key, i = cbor_decode(buf, i)
            item, i = cbor_decode(buf, i)
            out[key] = item
        return out, i
    if major == 6:
        return cbor_decode(buf, i)
    raise ValueError(f"Unsupported CBOR major type {major}")


def backend_of(codec_name: str):
    for backend in BACKENDS:
        if codec_name.lstrip("!") == f"zl.private.{backend}":
            return backend
    return None


def load_traces(directory: Path) -> dict:
    """Bytes fed to, and produced by, each backend over all traced files."""
    per_backend = defaultdict(lambda: {"streams": 0, "rawSize": 0, "compressedSize": 0})
    for path in sorted(directory.glob("*.cbor")):
        trace, _ = cbor_decode(path.read_bytes())
        for chunk in trace["chunks"]:
            streams = chunk["streams"]
            for codec in chunk["codecs"]:
                backend = backend_of(codec["name"])
                if backend is None:
                    continue
                sizes = [
                    streams[s]["contentSize"]
                    for s in codec["inputStreams"] + codec["outputStreams"]
                ]
                stats = per_backend[backend]
                stats["streams"] += 1
                # The larger stream is the raw data, the smaller one the
                # compressed payload (both directions of the trace work).
                stats["rawSize"] += max(sizes)
                stats["compressedSize"] += min(sizes)
    return dict(per_backend)


def load_trained(path: Path) -> dict:
    """Number of graphs of the trained compressor using each backend."""
    # `zli inspect` output looks like JSON but isn't (integer keys aren't
    # quoted), so just count the backend nodes listed by the graphs.
    text = path.read_text()
    counts = {}
    for backend in BACKENDS:
        n = len(re.findall(rf'"!?zl\.private\.{backend}"', text))
        if n:
            counts[backend] = n
    return counts


def improvement(base: dict, other: dict) -> float:
    """Reduction of the compressed size, in percent."""
    return 100.0 * (1 - other["compressedSize"] / base["compressedSize"])


def fmt(value, spec):
    return "-" if value is None else format(value, spec)


def main() -> None:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=here / "results")
    args = parser.parse_args()
    results = args.results

    bench = {p.stem: load_bench(p) for p in sorted((results / "bench").glob("*.csv"))}
    traces = {
        p.name: load_traces(p)
        for p in sorted((results / "traces").glob("*"))
        if p.is_dir()
    }
    trained = {
        p.stem: load_trained(p) for p in sorted((results / "trained").glob("*.json"))
    }

    comparisons = []

    def compare(label, base_name, other_name):
        if base_name in bench and other_name in bench:
            comparisons.append(
                {
                    "label": label,
                    "base": base_name,
                    "other": other_name,
                    "sizeReductionPct": improvement(bench[base_name], bench[other_name]),
                }
            )

    masks = {1: "+deflate", 2: "+lzma2", 4: "+bzip3", 7: "+all"}
    for dataset in ["silesia_generic", "psam_csv"]:
        for mask, label in masks.items():
            compare(
                f"untrained {dataset} {label}",
                f"untrained_{dataset}_m0",
                f"untrained_{dataset}_m{mask}",
            )
    for dataset in ["silesia_serial", "sao", "psam_csv"]:
        compare(
            f"trained {dataset} ACE+extra",
            f"trained_{dataset}_base",
            f"trained_{dataset}_ext",
        )

    summary = {
        "bench": bench,
        "comparisons": comparisons,
        "traces": traces,
        "trained": trained,
    }
    (results / "summary.json").write_text(json.dumps(summary, indent=2))

    lines = ["# Backend search results", "", "## Benchmarks", ""]
    lines.append("| run | files | ratio | compress MB/s | decompress MB/s |")
    lines.append("|---|---:|---:|---:|---:|")
    for name, b in bench.items():
        lines.append(
            f"| {name} | {b['files']} | {fmt(b['ratio'], '.3f')} | "
            f"{fmt(b['cMBps'], '.2f')} | {fmt(b['dMBps'], '.1f')} |"
        )
    lines += ["", "## Compressed size reduction vs zstd only / baseline ACE", ""]
    lines.append("| comparison | size reduction |")
    lines.append("|---|---:|")
    for c in comparisons:
        lines.append(f"| {c['label']} | {c['sizeReductionPct']:+.2f}% |")
    lines += ["", "## Bytes handled by each backend (traces)", ""]
    lines.append("| run | backend | streams | raw bytes | compressed bytes | ratio |")
    lines.append("|---|---|---:|---:|---:|---:|")
    for run, per_backend in traces.items():
        for backend, s in sorted(per_backend.items()):
            ratio = s["rawSize"] / s["compressedSize"] if s["compressedSize"] else None
            lines.append(
                f"| {run} | {backend} | {s['streams']} | {s['rawSize']} | "
                f"{s['compressedSize']} | {fmt(ratio, '.2f')} |"
            )
    lines += ["", "## Backends used by the trained compressors", ""]
    lines.append("| compressor | " + " | ".join(BACKENDS) + " |")
    lines.append("|---|" + "---:|" * len(BACKENDS))
    for name, counts in trained.items():
        lines.append(
            f"| {name} | " + " | ".join(str(counts.get(b, 0)) for b in BACKENDS) + " |"
        )
    (results / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
