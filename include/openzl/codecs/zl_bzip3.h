// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BZIP3_H
#define OPENZL_CODECS_BZIP3_H

#include "openzl/zl_errors.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/// Compresses a serial input with bzip3 (BWT based), as implemented by
/// libbzip3. bzip3 has no compression level: the global compression level is
/// ignored. The block size defaults to 16 MiB, and is never larger than the
/// input.
#define ZL_GRAPH_BZIP3 ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_bzip3)

/// @returns ZL_GRAPH_BZIP3 with overridden block size, in bytes,
/// which must be within [65 KiB, 511 MiB]
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildBzip3Graph(ZL_Compressor* cgraph, int blockSize);

/// Set this integer parameter to override the block size, in bytes
#define ZL_BZIP3_BLOCK_SIZE_PID 0

#if defined(__cplusplus)
}
#endif

#endif
