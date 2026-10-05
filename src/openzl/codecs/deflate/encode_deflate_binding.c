// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/deflate/encode_deflate_binding.h"
#include <stdint.h>
#include "openzl/codecs/zl_deflate.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardNodeID_deflate
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h"

#include <zlib.h>

ZL_Report EI_deflate(ZL_Encoder* eic, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eic);
    ZL_ASSERT_NN(eic);
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbIns, 1);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);
    // zlib's one-shot interface is limited to 32-bit sizes
    ZL_ERR_IF_GT(inSize, UINT32_MAX, node_invalid_input);

    // By default use the global compression level
    int cLevel = ZL_Encoder_getCParam(eic, ZL_CParam_compressionLevel);

    // Get the compression level override, if it exists
    ZL_IntParam cLevelParam = ZL_Encoder_getLocalIntParam(
            eic, ZL_DEFLATE_COMPRESSION_LEVEL_OVERRIDE_PID);
    if (cLevelParam.paramId == ZL_DEFLATE_COMPRESSION_LEVEL_OVERRIDE_PID) {
        cLevel = cLevelParam.paramValue;
    }
    cLevel = ZL_MAX(1, ZL_MIN(9, cLevel));

    // Write the original size as a varint
    uint8_t header[ZL_VARINT_LENGTH_64];
    size_t headerSize = ZL_varintEncode((uint64_t)inSize, header);
    ZL_Encoder_sendCodecHeader(eic, header, headerSize);

    if (inSize == 0) {
        ZL_Output* const out = ZL_Encoder_createTypedStream(eic, 0, 1, 1);
        ZL_ERR_IF_NULL(out, allocation);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        return ZL_returnSuccess();
    }

    z_stream strm = { 0 };
    ZL_ERR_IF_NE(
            deflateInit2(
                    &strm,
                    cLevel,
                    Z_DEFLATED,
                    -15, // raw deflate: no zlib header nor adler32
                    8,
                    Z_DEFAULT_STRATEGY),
            Z_OK,
            allocation,
            "deflateInit2 failed");

    // Allocate the output buffer
    const size_t outCapacity = deflateBound(&strm, (uLong)inSize);
    ZL_Output* const out = ZL_Encoder_createTypedStream(eic, 0, outCapacity, 1);
    if (out == NULL || outCapacity > UINT32_MAX) {
        deflateEnd(&strm);
        ZL_ERR(allocation);
    }

    // Do the compression
    strm.next_in   = (Bytef*)(uintptr_t)ZL_Input_ptr(in);
    strm.avail_in  = (uInt)inSize;
    strm.next_out  = (Bytef*)ZL_Output_ptr(out);
    strm.avail_out = (uInt)outCapacity;
    const int ret  = deflate(&strm, Z_FINISH);
    const size_t compressedSize = (size_t)strm.total_out;
    deflateEnd(&strm);
    ZL_ERR_IF_NE(ret, Z_STREAM_END, GENERIC, "deflate failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, compressedSize));

    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildDeflateGraph(ZL_Compressor* compressor, int compressionLevel)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);
    ZL_IntParam intParam = { ZL_DEFLATE_COMPRESSION_LEVEL_OVERRIDE_PID,
                             compressionLevel };

    ZL_LocalParams localParams = {
        .intParams = { &intParam, 1 },
    };
    ZL_GraphParameters desc = {
        .localParams = &localParams,
    };
    return ZL_Compressor_parameterizeGraph(compressor, ZL_GRAPH_DEFLATE, &desc);
}
