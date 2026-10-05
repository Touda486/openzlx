// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_LZMA2_DECODE_LZMA2_BINDING_H
#define ZSTRONG_CODECS_LZMA2_DECODE_LZMA2_BINDING_H

#include "openzl/zl_dtransform.h"

ZL_Report DI_lzma2(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_LZMA2(id)                        \
    {                                       \
        .transform_f = DI_lzma2,            \
        .name        = "!zl.private.lzma2", \
    }

#endif // ZSTRONG_CODECS_LZMA2_DECODE_LZMA2_BINDING_H
