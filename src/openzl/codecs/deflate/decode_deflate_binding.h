// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_DEFLATE_DECODE_DEFLATE_BINDING_H
#define ZSTRONG_CODECS_DEFLATE_DECODE_DEFLATE_BINDING_H

#include "openzl/zl_dtransform.h"

ZL_Report DI_deflate(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_DEFLATE(id)                        \
    {                                         \
        .transform_f = DI_deflate,            \
        .name        = "!zl.private.deflate", \
    }

#endif // ZSTRONG_CODECS_DEFLATE_DECODE_DEFLATE_BINDING_H
