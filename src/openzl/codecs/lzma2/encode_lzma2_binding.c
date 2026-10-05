// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lzma2/encode_lzma2_binding.h"
#include <stdint.h>
#include "openzl/codecs/zl_lzma2.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardNodeID_lzma2
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h"

#include <lzma.h>

ZL_Report EI_lzma2(ZL_Encoder* eic, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eic);
    ZL_ASSERT_NN(eic);
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbIns, 1);

    const ZL_Input* in = ins[0];
    size_t inSize      = ZL_Input_numElts(in);

    // By default use the global compression level
    int cLevel = ZL_Encoder_getCParam(eic, ZL_CParam_compressionLevel);

    // Get the compression level override, if it exists
    ZL_IntParam cLevelParam = ZL_Encoder_getLocalIntParam(
            eic, ZL_LZMA2_COMPRESSION_LEVEL_OVERRIDE_PID);
    if (cLevelParam.paramId == ZL_LZMA2_COMPRESSION_LEVEL_OVERRIDE_PID) {
        cLevel = cLevelParam.paramValue;
    }
    cLevel = ZL_MAX(0, ZL_MIN(9, cLevel));

    lzma_options_lzma opt;
    ZL_ERR_IF(
            lzma_lzma_preset(&opt, (uint32_t)cLevel),
            GENERIC,
            "lzma_lzma_preset failed");
    // A dictionary larger than the input is useless, and costs memory on both
    // the compression and decompression sides.
    const uint64_t maxUsefulDict =
            ZL_MAX((uint64_t)inSize, (uint64_t)LZMA_DICT_SIZE_MIN);
    if ((uint64_t)opt.dict_size > maxUsefulDict) {
        opt.dict_size = (uint32_t)maxUsefulDict;
    }
    lzma_filter filters[2] = {
        { .id = LZMA_FILTER_LZMA2, .options = &opt },
        { .id = LZMA_VLI_UNKNOWN, .options = NULL },
    };

    // Header: original size as a varint, then the 1-byte LZMA2 properties
    uint8_t header[ZL_VARINT_LENGTH_64 + 1];
    size_t headerSize = ZL_varintEncode((uint64_t)inSize, header);
    uint32_t propsSize;
    ZL_ERR_IF_NE(
            lzma_properties_size(&propsSize, &filters[0]),
            LZMA_OK,
            GENERIC,
            "lzma_properties_size failed");
    ZL_ERR_IF_NE(propsSize, 1, GENERIC, "Unexpected LZMA2 properties size");
    ZL_ERR_IF_NE(
            lzma_properties_encode(&filters[0], header + headerSize),
            LZMA_OK,
            GENERIC,
            "lzma_properties_encode failed");
    headerSize += 1;
    ZL_Encoder_sendCodecHeader(eic, header, headerSize);

    if (inSize == 0) {
        ZL_Output* const out = ZL_Encoder_createTypedStream(eic, 0, 1, 1);
        ZL_ERR_IF_NULL(out, allocation);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        return ZL_returnSuccess();
    }

    // Allocate the output buffer.
    // The xz stream bound is a superset of the raw LZMA2 bound.
    const size_t outCapacity = lzma_stream_buffer_bound(inSize);
    ZL_ERR_IF_EQ(outCapacity, 0, node_invalid_input, "Input too large");
    ZL_Output* const out = ZL_Encoder_createTypedStream(eic, 0, outCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);

    // Do the compression
    size_t outPos       = 0;
    const lzma_ret lret = lzma_raw_buffer_encode(
            filters,
            NULL,
            (const uint8_t*)ZL_Input_ptr(in),
            inSize,
            (uint8_t*)ZL_Output_ptr(out),
            &outPos,
            outCapacity);
    ZL_ERR_IF_NE(lret, LZMA_OK, GENERIC, "lzma_raw_buffer_encode failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, outPos));

    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildLzma2Graph(ZL_Compressor* compressor, int compressionLevel)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);
    ZL_IntParam intParam = { ZL_LZMA2_COMPRESSION_LEVEL_OVERRIDE_PID,
                             compressionLevel };

    ZL_LocalParams localParams = {
        .intParams = { &intParam, 1 },
    };
    ZL_GraphParameters desc = {
        .localParams = &localParams,
    };
    return ZL_Compressor_parameterizeGraph(compressor, ZL_GRAPH_LZMA2, &desc);
}
