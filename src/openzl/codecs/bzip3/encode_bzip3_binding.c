// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bzip3/encode_bzip3_binding.h"
#include <stdint.h>
#include <string.h>
#include "openzl/codecs/bzip3/common_bzip3.h"
#include "openzl/codecs/zl_bzip3.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardNodeID_bzip3
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h"

#include <libbz3.h>

/// Compresses @p in block by block into @p out, which must be able to hold
/// ZL_BZIP3_COMPRESS_BOUND() bytes. @returns the compressed size.
static ZL_Report EI_bzip3_compressBlocks(
        ZL_Encoder* eic,
        struct bz3_state* state,
        const uint8_t* in,
        size_t inSize,
        size_t blockSize,
        uint8_t* out)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eic);
    uint8_t* const scratch =
            (uint8_t*)ZL_Encoder_getScratchSpace(eic, bz3_bound(blockSize));
    ZL_ERR_IF_NULL(scratch, allocation);

    uint8_t* op = out;
    for (size_t pos = 0; pos < inSize; pos += blockSize) {
        const size_t blockLen = ZL_MIN(blockSize, inSize - pos);
        memcpy(scratch, in + pos, blockLen);
        // bz3_encode_block() works in place, in a bz3_bound() sized buffer
        const int32_t encodedSize =
                bz3_encode_block(state, scratch, (int32_t)blockLen);
        ZL_ERR_IF_LT(encodedSize, 0, GENERIC, "bz3_encode_block failed");
        ZL_ERR_IF_GT((size_t)encodedSize, bz3_bound(blockLen), logicError);

        // Store the block raw when bzip3 doesn't shrink it
        const int isRaw          = (size_t)encodedSize >= blockLen;
        const size_t payloadSize = isRaw ? blockLen : (size_t)encodedSize;
        op += ZL_varintEncode(
                ((uint64_t)payloadSize << 1) | (uint64_t)isRaw, op);
        memcpy(op, isRaw ? in + pos : scratch, payloadSize);
        op += payloadSize;
    }
    return ZL_returnValue((size_t)(op - out));
}

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

    // Header: the original size, then the block size, as varints
    uint8_t header[2 * ZL_VARINT_LENGTH_64];
    size_t headerSize = ZL_varintEncode((uint64_t)inSize, header);
    headerSize += ZL_varintEncode((uint64_t)blockSize, header + headerSize);
    ZL_Encoder_sendCodecHeader(eic, header, headerSize);

    // Allocate the output buffer
    const size_t nbBlocks = (inSize + blockSize - 1) / blockSize;
    const size_t outCapacity =
            ZL_BZIP3_COMPRESS_BOUND(inSize, blockSize, nbBlocks);
    ZL_ERR_IF_LT(outCapacity, inSize, node_invalid_input, "Input too large");
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eic, 0, ZL_MAX(outCapacity, 1), 1);
    ZL_ERR_IF_NULL(out, allocation);
    if (inSize == 0) {
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        return ZL_returnSuccess();
    }

    // Do the compression
    struct bz3_state* const state = bz3_new((int32_t)blockSize);
    ZL_ERR_IF_NULL(state, allocation);
    const ZL_Report compressedSize = EI_bzip3_compressBlocks(
            eic,
            state,
            (const uint8_t*)ZL_Input_ptr(in),
            inSize,
            blockSize,
            (uint8_t*)ZL_Output_ptr(out));
    bz3_free(state);
    ZL_ERR_IF_ERR(compressedSize);
    ZL_ERR_IF_GT(ZL_validResult(compressedSize), outCapacity, logicError);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, ZL_validResult(compressedSize)));

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
