# Bzip3 Decoder Specification

### Inputs
The decoder takes a single serial stream containing the compressed payload.

### Codec Header
1. A varint (LEB128) containing the size in bytes of the decompressed output,
   $n$.
2. A varint containing the block size $B$.

The header must not contain any other byte. $B$ must satisfy
$65 \cdot 2^{10} \le B \le 511 \cdot 2^{20}$ and $B \le \max(n, 65 \cdot 2^{10})$.
This bounds the memory that a corrupted header can make the decoder allocate.

### Decoding
If $n = 0$, the payload must be empty, and the output is empty.

Otherwise, the output is split into $\lceil n / B \rceil$ blocks of $B$ bytes,
except the last one, which holds the remaining bytes. Let $L_i$ be the size of
block $i$. The payload contains each block in order, as:
1. A varint $t_i$. The low bit $r_i = t_i \mathbin{\&} 1$ tells whether the
   block is stored raw, and $s_i = t_i \gg 1$ is the size of the block payload.
2. $s_i$ bytes of block payload:
   * If $r_i = 1$, the payload is the block itself, and $s_i$ must equal $L_i$.
   * If $r_i = 0$, the payload is a single bzip3 block, as produced by
     `bz3_encode_block()` from
     [libbzip3](https://github.com/kspalaiologos/bzip3) for a state created
     with block size $B$, and $s_i$ must be smaller than $L_i$. It is decoded
     with `bz3_decode_block()` using an original size of $L_i$, and must
     decode to exactly $L_i$ bytes. The bzip3 block embeds its own CRC32.

It is an error if:
* A block payload extends past the end of the payload.
* A bzip3 block is invalid according to libbzip3, including CRC mismatches.
* The payload contains any byte after the last block.

### Outputs
A single serial stream of $n$ bytes.

### Encoder notes
These notes don't affect decoding.
* bzip3 has no compression level. The block size defaults to 16 MiB (the
  default of the bzip3 command line tool), can be overridden within
  $[65 \cdot 2^{10}, 511 \cdot 2^{20}]$, and is clamped to
  $\max(n, 65 \cdot 2^{10})$.
* A block is stored raw when bzip3 doesn't make it smaller, so the payload is
  never larger than $n$ plus one varint per block.
* The codec uses libbzip3's low level block API on purpose. Its frame API
  (`bz3_compress()` / `bz3_decompress()`) has bugs in libbzip3 1.5.3: it drops
  the last block when $n$ is a multiple of the block size, writes past
  `bz3_bound()` on small incompressible inputs, and fails to decode frames
  whose blocks expanded.
