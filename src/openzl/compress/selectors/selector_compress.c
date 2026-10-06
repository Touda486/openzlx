// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/selector_compress.h"

#include <string.h> // memcpy

#include "openzl/common/assertion.h"
#include "openzl/codecs/zl_bzip3.h"
#include "openzl/codecs/zl_deflate.h"
#include "openzl/codecs/zl_lzma2.h"
#include "openzl/compress/private_nodes.h" // ZS2_GRAPH_COMPRESS_*
#include "openzl/compress/selectors/selector_brute_force.h"
#include "openzl/zl_compress.h" // ZL_CParam_serialBackendSearch
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"

ZL_Report
MultiInputGraph_compress(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_DLOG(SEQ, "MultiInputGraph_compress: %zu inputs", nbInputs);
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputs[n], ZL_GRAPH_COMPRESS1));
    }
    return ZL_returnSuccess();
}

/* SI_selector_compress():
 *
 * Dispatch to type-specific compress selectors.
 */
ZL_GraphID SI_selector_compress(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    ZL_Type const st = ZL_Input_type(inputStream);
    ZL_DLOG(BLOCK, "SI_selector_compress (inType=%u)", st);

    ZL_GraphID graph;

    switch (st) {
        case ZL_Type_serial:
            graph = ZL_GRAPH_SERIAL_COMPRESS;
            break;
        case ZL_Type_struct:
            graph = ZL_GRAPH_STRUCT_COMPRESS;
            break;
        case ZL_Type_numeric:
            graph = ZL_GRAPH_NUMERIC_COMPRESS;
            break;
        case ZL_Type_string:
            graph = ZL_GRAPH_STRING_COMPRESS;
            break;
        default:
            ZL_ASSERT_FAIL("invalid stream type");
            return ZL_GRAPH_ILLEGAL;
    }
    return graph;
}

/* The backend search is costly: each candidate compresses the entire input.
 * For large inputs, candidates are first ranked on a sample, made of evenly
 * spaced slices of the input, and only the ones close to the best one are
 * tried on the entire input. */
#define SERIAL_SEARCH_SAMPLE_SLICES 8
// Candidates whose sample is at most this much larger than the best one
// are tried on the entire input
#define SERIAL_SEARCH_MARGIN_PCT 1

/* @returns the number of candidates kept in @candidates, in their original
 * order, or @nbCandidates when sampling isn't possible. */
static size_t SI_preselectOnSample(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        ZL_GraphID candidates[],
        size_t nbCandidates,
        size_t sampleSize)
{
    size_t const srcSize   = ZL_Input_contentSize(input);
    size_t const sliceSize = sampleSize / SERIAL_SEARCH_SAMPLE_SLICES;
    sampleSize             = sliceSize * SERIAL_SEARCH_SAMPLE_SLICES;
    if (sliceSize == 0 || srcSize / 2 < sampleSize) {
        return nbCandidates;
    }
    char* const sample = ZL_Selector_getScratchSpace(selCtx, sampleSize);
    if (sample == NULL) {
        return nbCandidates;
    }
    const char* const src = (const char*)ZL_Input_ptr(input);
    size_t const stride =
            (srcSize - sliceSize) / (SERIAL_SEARCH_SAMPLE_SLICES - 1);
    for (size_t n = 0; n < SERIAL_SEARCH_SAMPLE_SLICES; n++) {
        memcpy(sample + n * sliceSize, src + n * stride, sliceSize);
    }
    ZL_TypedRef* const ref = ZL_TypedRef_createSerial(sample, sampleSize);
    if (ref == NULL) {
        return nbCandidates;
    }
    ZL_GraphReport reports[4];
    size_t sizes[4];
    size_t best = (size_t)-1;
    ZL_ASSERT_LE(nbCandidates, 4);
    ZL_Selector_tryGraphs(selCtx, ref, candidates, nbCandidates, reports);
    for (size_t n = 0; n < nbCandidates; n++) {
        sizes[n] = ZL_isError(reports[n].finalCompressedSize)
                ? (size_t)-1
                : ZL_validResult(reports[n].finalCompressedSize);
        if (sizes[n] < best) {
            best = sizes[n];
        }
    }
    ZL_TypedRef_free(ref);
    if (best == (size_t)-1) {
        return nbCandidates;
    }
    size_t const limit = best + best * SERIAL_SEARCH_MARGIN_PCT / 100;
    size_t nbKept      = 0;
    for (size_t n = 0; n < nbCandidates; n++) {
        if (sizes[n] <= limit) {
            candidates[nbKept++] = candidates[n];
        }
    }
    return nbKept;
}

ZL_GraphID SI_selector_compress_serial(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_serial);
    (void)customGraphs;
    (void)nbCustomGraphs;
    // Experimental: when requested, arbitrate between several general purpose
    // backends by trying each of them, and keep the smallest result.
    const int search =
            ZL_Selector_getCParam(selCtx, ZL_CParam_serialBackendSearch);
    // Backends unsupported by the format version are skipped.
    if (search > 0) {
        ZL_GraphID candidates[4];
        size_t nbCandidates = 0;
        if (search & ZL_SerialBackendSearch_zstd) {
            candidates[nbCandidates++] = ZL_GRAPH_ZSTD;
        }
        if ((search & ZL_SerialBackendSearch_deflate)
            && ZL_Selector_isNodeSupported(
                    selCtx,
                    ZL_MAKE_NODE_ID(ZL_PrivateStandardNodeID_deflate))) {
            candidates[nbCandidates++] = ZL_GRAPH_DEFLATE;
        }
        if ((search & ZL_SerialBackendSearch_lzma2)
            && ZL_Selector_isNodeSupported(
                    selCtx, ZL_MAKE_NODE_ID(ZL_PrivateStandardNodeID_lzma2))) {
            candidates[nbCandidates++] = ZL_GRAPH_LZMA2;
        }
        if ((search & ZL_SerialBackendSearch_bzip3)
            && ZL_Selector_isNodeSupported(
                    selCtx, ZL_MAKE_NODE_ID(ZL_PrivateStandardNodeID_bzip3))) {
            candidates[nbCandidates++] = ZL_GRAPH_BZIP3;
        }
        const int sampleSize = ZL_Selector_getCParam(
                selCtx, ZL_CParam_serialBackendSearchSampleSize);
        if (nbCandidates > 1 && sampleSize > 0) {
            nbCandidates = SI_preselectOnSample(
                    selCtx,
                    inputStream,
                    candidates,
                    nbCandidates,
                    (size_t)sampleSize);
        }
        if (nbCandidates == 1) {
            return candidates[0];
        }
        if (nbCandidates > 1) {
            return SI_selector_brute_force(
                    selCtx, inputStream, candidates, nbCandidates);
        }
    }
    // In the future, we will probably arbitrate here between several methods.
    // Other LZ engines such as FastLZ and ROLZ come to mind.
    // Might even compete with Huffman or STORE.
    // This will require a more refined selector, maybe ML driven.
    // For the time being, just defer to zstd as a generic well-proven backup.
    return ZL_GRAPH_ZSTD;
}

ZL_GraphID SI_selector_compress_struct(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_struct);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    return ZL_GRAPH_FIELD_LZ;
}

ZL_GraphID SI_selector_compress_numeric(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    /* Preserve the private numeric-compress graph as a compatibility shim. */
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_numeric);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    return ZL_GRAPH_NUMERIC;
}

ZL_GraphID SI_selector_compress_string(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_string);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    // For the time being, just split VSF Stream into its components,
    // and compress them independently, using generic compression graphs.
    // In the future, more specialized compressors, dedicated to variable size
    // fields, might compete with this simple generic split strategy.
    return ZL_GRAPH_STRING_SEPARATE_COMPRESS;
}
