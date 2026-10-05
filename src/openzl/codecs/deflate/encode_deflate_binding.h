// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_DEFLATE_ENCODE_DEFLATE_BINDING_H
#define ZSTRONG_CODECS_DEFLATE_ENCODE_DEFLATE_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/// Encode with raw deflate (zlib).
/// Takes a serial input.
ZL_Report EI_deflate(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_DEFLATE(id)                        \
    {                                         \
        .gd          = PIPE_GRAPH(id),        \
        .transform_f = EI_deflate,            \
        .name        = "!zl.private.deflate", \
    }

ZL_END_C_DECLS

#endif // ZSTRONG_CODECS_DEFLATE_ENCODE_DEFLATE_BINDING_H
