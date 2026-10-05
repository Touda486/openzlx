// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_BZIP3_COMMON_BZIP3_H
#define ZSTRONG_CODECS_BZIP3_COMMON_BZIP3_H

/// Block size limits accepted by libbzip3
#define ZL_BZIP3_MIN_BLOCK_SIZE ((size_t)65 << 10)
#define ZL_BZIP3_MAX_BLOCK_SIZE ((size_t)511 << 20)

/// Same default block size as the bzip3 command line tool
#define ZL_BZIP3_DEFAULT_BLOCK_SIZE ((size_t)16 << 20)

/// Size of the frame header written by bz3_compress():
/// "BZ3v1" magic, 32-bit LE block size, 32-bit LE block count
#define ZL_BZIP3_FRAME_HEADER_SIZE 13

#endif // ZSTRONG_CODECS_BZIP3_COMMON_BZIP3_H
