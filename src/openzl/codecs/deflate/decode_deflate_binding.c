// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/deflate/decode_deflate_binding.h"
#include <stdint.h>
#include "openzl/common/assertion.h"
#include "openzl/shared/varint.h"

#include <zlib.h>

ZL_Report DI_deflate(ZL_Decoder* dic, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dic);
    ZL_ASSERT_NN(dic);
    ZL_ASSERT_NN(ins);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);
    ZL_ERR_IF_GT(inSize, UINT32_MAX, corruption);

    // Read the original size from the header
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dic);
    ZL_ERR_IF_EQ(header.size, 0, corruption, "No header provided");
    const uint8_t* headerStart = (const uint8_t*)header.start;
    const uint8_t* headerEnd   = (const uint8_t*)header.start + header.size;
    ZL_TRY_LET_CONST(
            uint64_t, outSize, ZL_varintDecode(&headerStart, headerEnd));
    ZL_ERR_IF(headerStart != headerEnd, corruption, "Trailing header bytes");
    ZL_ERR_IF_GT(outSize, UINT32_MAX, corruption);

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
    z_stream strm = { 0 };
    ZL_ERR_IF_NE(
            inflateInit2(&strm, -15), Z_OK, allocation, "inflateInit2 failed");
    strm.next_in   = (Bytef*)(uintptr_t)ZL_Input_ptr(in);
    strm.avail_in  = (uInt)inSize;
    strm.next_out  = (Bytef*)ZL_Output_ptr(out);
    strm.avail_out = (uInt)outSize;
    const int ret  = inflate(&strm, Z_FINISH);
    const size_t decompressedSize = (size_t)strm.total_out;
    const uInt remainingIn        = strm.avail_in;
    inflateEnd(&strm);
    ZL_ERR_IF_NE(ret, Z_STREAM_END, corruption, "inflate failed");
    ZL_ERR_IF_NE(decompressedSize, outSize, corruption, "Size mismatch");
    ZL_ERR_IF_NE(remainingIn, 0, corruption, "Trailing payload bytes");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, decompressedSize));

    return ZL_returnSuccess();
}
