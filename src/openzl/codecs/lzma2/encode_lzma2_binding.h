// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_LZMA2_ENCODE_LZMA2_BINDING_H
#define ZSTRONG_CODECS_LZMA2_ENCODE_LZMA2_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/// Encode with raw LZMA2 (liblzma, the algorithm of xz).
/// Takes a serial input.
ZL_Report EI_lzma2(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_LZMA2(id)                        \
    {                                       \
        .gd          = PIPE_GRAPH(id),      \
        .transform_f = EI_lzma2,            \
        .name        = "!zl.private.lzma2", \
    }

ZL_END_C_DECLS

#endif // ZSTRONG_CODECS_LZMA2_ENCODE_LZMA2_BINDING_H
