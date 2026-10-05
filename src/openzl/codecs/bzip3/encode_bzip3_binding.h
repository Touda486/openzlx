// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_BZIP3_ENCODE_BZIP3_BINDING_H
#define ZSTRONG_CODECS_BZIP3_ENCODE_BZIP3_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/// Encode with bzip3 (libbzip3).
/// Takes a serial input.
ZL_Report EI_bzip3(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_BZIP3(id)                        \
    {                                       \
        .gd          = PIPE_GRAPH(id),      \
        .transform_f = EI_bzip3,            \
        .name        = "!zl.private.bzip3", \
    }

ZL_END_C_DECLS

#endif // ZSTRONG_CODECS_BZIP3_ENCODE_BZIP3_BINDING_H
