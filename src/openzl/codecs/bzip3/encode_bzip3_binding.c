// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bzip3/encode_bzip3_binding.h"
#include <stdint.h>
#include "openzl/codecs/bzip3/common_bzip3.h"
#include "openzl/codecs/zl_bzip3.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardNodeID_bzip3
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h"

#include <libbz3.h>

ZL_Report EI_bzip3(ZL_Encoder* eic, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eic);
    ZL_ASSERT_NN(eic);
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbIns, 1);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);

    // bzip3 has no compression level. The block size is the only knob.
    size_t blockSize = ZL_BZIP3_DEFAULT_BLOCK_SIZE;
    ZL_IntParam blockSizeParam =
            ZL_Encoder_getLocalIntParam(eic, ZL_BZIP3_BLOCK_SIZE_PID);
    if (blockSizeParam.paramId == ZL_BZIP3_BLOCK_SIZE_PID) {
        ZL_ERR_IF_LT(
                (int64_t)blockSizeParam.paramValue,
                (int64_t)ZL_BZIP3_MIN_BLOCK_SIZE,
                parameter_invalid);
        ZL_ERR_IF_GT(
                (int64_t)blockSizeParam.paramValue,
                (int64_t)ZL_BZIP3_MAX_BLOCK_SIZE,
                parameter_invalid);
        blockSize = (size_t)blockSizeParam.paramValue;
    }
    // A block larger than the input is useless, and costs memory on both
    // the compression and decompression sides. The decoder relies on this.
    blockSize = ZL_MIN(blockSize, ZL_MAX(inSize, ZL_BZIP3_MIN_BLOCK_SIZE));
    // libbzip3 (at least up to 1.5.3) silently drops the last block when the
    // input size is a multiple of the block size, so never pick such a block
    // size. This makes the block size at most 1 byte larger than the input.
    if (inSize > 0) {
        while (inSize % blockSize == 0) {
            ++blockSize;
        }
    }

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

    // Allocate the output buffer
    const size_t outCapacity = bz3_bound(inSize);
    ZL_ERR_IF_LT(outCapacity, inSize, node_invalid_input, "Input too large");
    ZL_Output* const out = ZL_Encoder_createTypedStream(eic, 0, outCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);

    // Do the compression
    size_t outSize = outCapacity;
    const int ret  = bz3_compress(
            (uint32_t)blockSize,
            (const uint8_t*)ZL_Input_ptr(in),
            (uint8_t*)ZL_Output_ptr(out),
            inSize,
            &outSize);
    ZL_ERR_IF_NE(ret, BZ3_OK, GENERIC, "bz3_compress failed");
    ZL_ERR_IF_GT(outSize, outCapacity, logicError);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, outSize));

    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildBzip3Graph(ZL_Compressor* compressor, int blockSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);
    ZL_IntParam intParam = { ZL_BZIP3_BLOCK_SIZE_PID, blockSize };

    ZL_LocalParams localParams = {
        .intParams = { &intParam, 1 },
    };
    ZL_GraphParameters desc = {
        .localParams = &localParams,
    };
    return ZL_Compressor_parameterizeGraph(compressor, ZL_GRAPH_BZIP3, &desc);
}
