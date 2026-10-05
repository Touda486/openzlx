// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_LZMA2_H
#define OPENZL_CODECS_LZMA2_H

#include "openzl/zl_errors.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/// Compresses a serial input with raw LZMA2 (the algorithm of xz),
/// as implemented by liblzma.
/// The compression level is used as the xz preset, clamped to [0, 9].
#define ZL_GRAPH_LZMA2 ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_lzma2)

/// @returns ZL_GRAPH_LZMA2 with overridden compression level
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildLzma2Graph(ZL_Compressor* cgraph, int compressionLevel);

/// Set this integer parameter to override the compression level
#define ZL_LZMA2_COMPRESSION_LEVEL_OVERRIDE_PID 0

#if defined(__cplusplus)
}
#endif

#endif
