# LZMA2 Decoder Specification

### Inputs
The decoder takes a single serial stream containing the compressed payload.

### Codec Header
1. A varint (LEB128) containing the size in bytes of the decompressed output,
   $n$.
2. A single byte $p$ containing the LZMA2 properties, as defined by the
   [xz file format](https://tukaani.org/xz/xz-file-format.txt) section 5.3.1.
   $p$ encodes the dictionary size $d$: if $p = 40$, $d = 2^{32} - 1$,
   otherwise $d = (2 | (p \mathbin{\&} 1)) \ll (\lfloor p / 2 \rfloor + 11)$.
   $p$ must be at most 40.

The header must not contain any other byte.

### Decoding
If $n = 0$, the payload must be empty, and the output is empty.

Otherwise, the payload is a single raw LZMA2 stream, without the xz container
(no stream header, block header, index, nor checksum), terminated by the LZMA2
end marker (a `0x00` control byte).

Since a match can never reach further back than the start of the output, the
decoder may use a dictionary of $\min(d, \max(n, 4096))$ bytes instead of $d$.

It is an error if:
* The LZMA2 stream is invalid, or isn't terminated by the end marker.
* The LZMA2 stream doesn't decode to exactly $n$ bytes.
* The payload contains any byte after the LZMA2 end marker.

### Outputs
A single serial stream of $n$ bytes.
