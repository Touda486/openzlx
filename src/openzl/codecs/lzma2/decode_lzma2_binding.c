// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lzma2/decode_lzma2_binding.h"
#include <stdint.h>
#include <string.h>
#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"

#include <lzma.h>

ZL_Report DI_lzma2(ZL_Decoder* dic, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dic);
    ZL_ASSERT_NN(dic);
    ZL_ASSERT_NN(ins);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);

    // Read the original size and the LZMA2 properties from the header
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dic);
    ZL_ERR_IF_EQ(header.size, 0, corruption, "No header provided");
    const uint8_t* headerStart = (const uint8_t*)header.start;
    const uint8_t* headerEnd   = (const uint8_t*)header.start + header.size;
    ZL_TRY_LET_CONST(
            uint64_t, outSize, ZL_varintDecode(&headerStart, headerEnd));
    ZL_ERR_IF(
            headerEnd - headerStart != 1,
            corruption,
            "Expected exactly 1 LZMA2 properties byte");
    const uint8_t props = *headerStart;
    ZL_ERR_IF_GT(props, 40, corruption, "Invalid LZMA2 properties");
#if SIZE_MAX < UINT64_MAX
    ZL_ERR_IF_GT(outSize, SIZE_MAX, corruption);
#endif

    // Allocate the output buffer
    ZL_Output* const out =
            ZL_Decoder_createTypedStream(dic, 0, (size_t)outSize, 1);
    ZL_ERR_IF_NULL(out, allocation);

    if (outSize == 0) {
        ZL_ERR_IF_NE(inSize, 0, corruption, "Expected empty payload");
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        return ZL_returnSuccess();
    }

    // Decode the dictionary size, as specified by the LZMA2 properties byte.
    // Match distances can never exceed the number of bytes decoded so far, so
    // a dictionary larger than the output is never needed. Clamp it so that
    // a corrupted header cannot request more memory than the output itself.
    uint64_t dictSize = (props == 40)
            ? UINT32_MAX
            : ((uint64_t)(2 | (props & 1)) << (props / 2 + 11));
    dictSize = ZL_MIN(
            dictSize, ZL_MAX(outSize, (uint64_t)LZMA_DICT_SIZE_MIN));

    lzma_options_lzma opt;
    memset(&opt, 0, sizeof(opt));
    opt.dict_size          = (uint32_t)dictSize;
    lzma_filter filters[2] = {
        { .id = LZMA_FILTER_LZMA2, .options = &opt },
        { .id = LZMA_VLI_UNKNOWN, .options = NULL },
    };

    // Do the decompression
    size_t inPos        = 0;
    size_t outPos       = 0;
    const lzma_ret lret = lzma_raw_buffer_decode(
            filters,
            NULL,
            (const uint8_t*)ZL_Input_ptr(in),
            &inPos,
            inSize,
            (uint8_t*)ZL_Output_ptr(out),
            &outPos,
            (size_t)outSize);
    ZL_ERR_IF_NE(lret, LZMA_OK, corruption, "lzma_raw_buffer_decode failed");
    ZL_ERR_IF_NE(outPos, outSize, corruption, "Size mismatch");
    ZL_ERR_IF_NE(inPos, inSize, corruption, "Trailing payload bytes");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, outPos));

    return ZL_returnSuccess();
}
