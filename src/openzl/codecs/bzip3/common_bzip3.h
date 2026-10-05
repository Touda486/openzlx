// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_BZIP3_COMMON_BZIP3_H
#define ZSTRONG_CODECS_BZIP3_COMMON_BZIP3_H

#include "openzl/shared/varint.h"

/// Block size limits accepted by libbzip3
#define ZL_BZIP3_MIN_BLOCK_SIZE ((size_t)65 << 10)
#define ZL_BZIP3_MAX_BLOCK_SIZE ((size_t)511 << 20)

/// Same default block size as the bzip3 command line tool
#define ZL_BZIP3_DEFAULT_BLOCK_SIZE ((size_t)16 << 20)

/// Upper bound of the payload size: each block is stored raw when bzip3
/// doesn't shrink it, behind a varint of its size.
#define ZL_BZIP3_COMPRESS_BOUND(inSize, blockSize, nbBlocks) \
    ((inSize) + (nbBlocks) * ZL_VARINT_LENGTH_64)

#endif // ZSTRONG_CODECS_BZIP3_COMMON_BZIP3_H
