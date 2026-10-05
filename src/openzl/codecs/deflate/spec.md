# Deflate Decoder Specification

### Inputs
The decoder takes a single serial stream containing the compressed payload.

### Codec Header
A varint (LEB128) containing the size in bytes of the decompressed output, $n$.
The header must not contain any other byte. $n$ must be at most $2^{32} - 1$.

### Decoding
If $n = 0$, the payload must be empty, and the output is empty.

Otherwise, the payload is a single raw deflate stream as defined by
[RFC 1951](https://www.rfc-editor.org/rfc/rfc1951), without the zlib
(RFC 1950) or gzip (RFC 1952) wrapper, so without any header nor checksum.
The deflate stream is decoded with a 32 KiB window (zlib `windowBits = -15`).

It is an error if:
* The deflate stream is invalid, or isn't terminated by a final block.
* The deflate stream doesn't decode to exactly $n$ bytes.
* The payload contains any byte after the end of the deflate stream.

### Outputs
A single serial stream of $n$ bytes.
