# Bzip3 Decoder Specification

### Inputs
The decoder takes a single serial stream containing the compressed payload.

### Codec Header
A varint (LEB128) containing the size in bytes of the decompressed output, $n$.
The header must not contain any other byte.

### Decoding
If $n = 0$, the payload must be empty, and the output is empty.

Otherwise, the payload is a single bzip3 frame, as produced by `bz3_compress()`
from [libbzip3](https://github.com/kspalaiologos/bzip3) and consumed by
`bz3_decompress()`. All integers of the frame are little-endian:
1. The 5 byte magic `BZ3v1`.
2. A 4-byte block size $B$.
3. A 4-byte number of blocks.
4. For each block: a 4-byte compressed size, a 4-byte decompressed size, then
   the compressed block, which embeds its own CRC32.

The decoder must check, before decoding, that
$65 \cdot 2^{10} \le B \le \max(\mathrm{bound}(n + 1), 65 \cdot 2^{10})$,
where $\mathrm{bound}$ is libbzip3's `bz3_bound()`. This limits the memory a
corrupted frame can request. The encoder never requests a block size larger
than $\max(n + 1, 65 \cdot 2^{10})$, and libbzip3 never declares more than
`bz3_bound()` of the requested block size.

It is an error if:
* The frame is invalid according to libbzip3, including CRC mismatches.
* The frame doesn't decode to exactly $n$ bytes.

### Outputs
A single serial stream of $n$ bytes.

### Encoder notes
These notes don't affect decoding.
* bzip3 has no compression level. The block size defaults to 16 MiB, can be
  overridden within $[65 \cdot 2^{10}, 511 \cdot 2^{20}]$, and is clamped to
  $\max(n, 65 \cdot 2^{10})$.
* libbzip3 (at least up to 1.5.3) silently drops the last block when $n$ is a
  multiple of the block size. The encoder increments the block size until it
  doesn't divide $n$.
