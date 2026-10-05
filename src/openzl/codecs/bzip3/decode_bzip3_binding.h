// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_BZIP3_DECODE_BZIP3_BINDING_H
#define ZSTRONG_CODECS_BZIP3_DECODE_BZIP3_BINDING_H

#include "openzl/zl_dtransform.h"

ZL_Report DI_bzip3(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_BZIP3(id)                        \
    {                                       \
        .transform_f = DI_bzip3,            \
        .name        = "!zl.private.bzip3", \
    }

#endif // ZSTRONG_CODECS_BZIP3_DECODE_BZIP3_BINDING_H
