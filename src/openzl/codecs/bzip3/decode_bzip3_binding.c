// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bzip3/decode_bzip3_binding.h"
#include <stdint.h>
#include <string.h>
#include "openzl/codecs/bzip3/common_bzip3.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"

#include <libbz3.h>

/// Decompresses the blocks of @p src into @p dst, which holds @p dstSize
/// bytes, all of which must be produced.
static ZL_Report DI_bzip3_decompressBlocks(
        ZL_Decoder* dic,
        struct bz3_state* state,
        const uint8_t* src,
        size_t srcSize,
        size_t blockSize,
        uint8_t* dst,
        size_t dstSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dic);
    // bz3_decode_block() may need up to bz3_bound(blockLen) bytes of buffer
    const size_t scratchSize = bz3_bound(blockSize);
    uint8_t* const scratch =
            (uint8_t*)ZL_Decoder_getScratchSpace(dic, scratchSize);
    ZL_ERR_IF_NULL(scratch, allocation);

    const uint8_t* ip        = src;
    const uint8_t* const end = src + srcSize;
    for (size_t pos = 0; pos < dstSize; pos += blockSize) {
        const size_t blockLen = ZL_MIN(blockSize, dstSize - pos);
        ZL_TRY_LET_CONST(uint64_t, tag, ZL_varintDecode(&ip, end));
        const int isRaw          = (int)(tag & 1);
        const uint64_t blockSrc  = tag >> 1;
        ZL_ERR_IF_GT(blockSrc, (uint64_t)(end - ip), corruption);
        const size_t payloadSize = (size_t)blockSrc;
        if (isRaw) {
            ZL_ERR_IF_NE(payloadSize, blockLen, corruption);
            memcpy(dst + pos, ip, blockLen);
        } else {
            ZL_ERR_IF_GE(payloadSize, blockLen, corruption);
            memcpy(scratch, ip, payloadSize);
            const int32_t decodedSize = bz3_decode_block(
                    state,
                    scratch,
                    scratchSize,
                    (int32_t)payloadSize,
                    (int32_t)blockLen);
            ZL_ERR_IF_NE(
                    (int64_t)decodedSize,
                    (int64_t)blockLen,
                    corruption,
                    "bz3_decode_block failed");
            memcpy(dst + pos, scratch, blockLen);
        }
        ip += payloadSize;
    }
    ZL_ERR_IF(ip != end, corruption, "Trailing payload bytes");
    return ZL_returnSuccess();
}

ZL_Report DI_bzip3(ZL_Decoder* dic, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dic);
    ZL_ASSERT_NN(dic);
    ZL_ASSERT_NN(ins);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);

    // Read the original size and the block size from the header
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dic);
    ZL_ERR_IF_EQ(header.size, 0, corruption, "No header provided");
    const uint8_t* headerStart = (const uint8_t*)header.start;
    const uint8_t* headerEnd   = (const uint8_t*)header.start + header.size;
    ZL_TRY_LET_CONST(
            uint64_t, outSize, ZL_varintDecode(&headerStart, headerEnd));
    ZL_TRY_LET_CONST(
            uint64_t, blockSize, ZL_varintDecode(&headerStart, headerEnd));
    ZL_ERR_IF(headerStart != headerEnd, corruption, "Trailing header bytes");
#if SIZE_MAX < UINT64_MAX
    ZL_ERR_IF_GT(outSize, SIZE_MAX, corruption);
#endif
    // The encoder never uses a block larger than the output, which bounds the
    // memory a corrupted header can request.
    ZL_ERR_IF_LT(blockSize, ZL_BZIP3_MIN_BLOCK_SIZE, corruption);
    ZL_ERR_IF_GT(blockSize, ZL_BZIP3_MAX_BLOCK_SIZE, corruption);
    ZL_ERR_IF_GT(
            blockSize,
            ZL_MAX(outSize, (uint64_t)ZL_BZIP3_MIN_BLOCK_SIZE),
            corruption,
            "Block size larger than the output");

    // Allocate the output buffer
    ZL_Output* const out =
            ZL_Decoder_createTypedStream(dic, 0, (size_t)outSize, 1);
    ZL_ERR_IF_NULL(out, allocation);

    if (outSize == 0) {
        ZL_ERR_IF_NE(inSize, 0, corruption, "Expected empty payload");
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        return ZL_returnSuccess();
    }

    // Do the decompression
    struct bz3_state* const state = bz3_new((int32_t)blockSize);
    ZL_ERR_IF_NULL(state, allocation);
    const ZL_Report report = DI_bzip3_decompressBlocks(
            dic,
            state,
            (const uint8_t*)ZL_Input_ptr(in),
            inSize,
            (size_t)blockSize,
            (uint8_t*)ZL_Output_ptr(out),
            (size_t)outSize);
    bz3_free(state);
    ZL_ERR_IF_ERR(report);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, (size_t)outSize));

    return ZL_returnSuccess();
}
