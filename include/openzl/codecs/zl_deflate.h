// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_DEFLATE_H
#define OPENZL_CODECS_DEFLATE_H

#include "openzl/zl_errors.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/// Compresses a serial input with raw deflate (the algorithm of zip & gzip),
/// as implemented by zlib.
/// The compression level is clamped to [1, 9].
#define ZL_GRAPH_DEFLATE ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_deflate)

/// @returns ZL_GRAPH_DEFLATE with overridden compression level
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildDeflateGraph(ZL_Compressor* cgraph, int compressionLevel);

/// Set this integer parameter to override the compression level
#define ZL_DEFLATE_COMPRESSION_LEVEL_OVERRIDE_PID 0

#if defined(__cplusplus)
}
#endif

#endif
