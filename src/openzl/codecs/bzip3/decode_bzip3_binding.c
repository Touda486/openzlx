// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bzip3/decode_bzip3_binding.h"
#include <stdint.h>
#include <string.h>
#include "openzl/codecs/bzip3/common_bzip3.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"

#include <libbz3.h>

ZL_Report DI_bzip3(ZL_Decoder* dic, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dic);
    ZL_ASSERT_NN(dic);
    ZL_ASSERT_NN(ins);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);
    const uint8_t* src = (const uint8_t*)ZL_Input_ptr(in);

    // Read the original size from the header
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dic);
    ZL_ERR_IF_EQ(header.size, 0, corruption, "No header provided");
    const uint8_t* headerStart = (const uint8_t*)header.start;
    const uint8_t* headerEnd   = (const uint8_t*)header.start + header.size;
    ZL_TRY_LET_CONST(
            uint64_t, outSize, ZL_varintDecode(&headerStart, headerEnd));
    ZL_ERR_IF(headerStart != headerEnd, corruption, "Trailing header bytes");
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

    // bz3_decompress() allocates working memory proportional to the block
    // size declared in the frame. The encoder never requests a block larger
    // than the input (+1 byte, see the encoder), and libbzip3 then declares
    // at most bz3_bound() of that. Reject larger blocks before libbzip3
    // allocates anything.
    ZL_ERR_IF_LT(inSize, ZL_BZIP3_FRAME_HEADER_SIZE, corruption);
    ZL_ERR_IF_NE(memcmp(src, "BZ3v1", 5), 0, corruption, "Bad bzip3 magic");
    const size_t blockSize = (size_t)ZL_readLE32(src + 5);
    ZL_ERR_IF_LT(blockSize, ZL_BZIP3_MIN_BLOCK_SIZE, corruption);
    ZL_ERR_IF_GT(
            blockSize,
            ZL_MAX(bz3_bound((size_t)outSize + 1), ZL_BZIP3_MIN_BLOCK_SIZE),
            corruption,
            "Block size larger than the output");

    // Do the decompression
    size_t decompressedSize = (size_t)outSize;
    const int ret           = bz3_decompress(
            src, (uint8_t*)ZL_Output_ptr(out), inSize, &decompressedSize);
    ZL_ERR_IF_NE(ret, BZ3_OK, corruption, "bz3_decompress failed");
    ZL_ERR_IF_NE(decompressedSize, outSize, corruption, "Size mismatch");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, decompressedSize));

    return ZL_returnSuccess();
}
