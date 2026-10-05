// Copyright (c) Meta Platforms, Inc. and affiliates.

// Multi-threaded compression (ZL_CParam_nbWorkers) must produce the exact
// same frame, errors and warnings as serial compression.

#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/codecs/zl_bzip3.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_deflate.h"
#include "openzl/codecs/zl_entropy.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_lzma2.h"
#include "openzl/codecs/zl_split.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/common/threading.h" // ZL_MULTITHREAD
#include "openzl/compress/cctx.h"      // CCTX_setMTTestingFlags
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_version.h"

namespace {

/// Mix of compressible numeric and textual content
std::string genData(size_t size, uint32_t seed)
{
    std::mt19937 gen(seed);
    std::string data;
    data.reserve(size + 16);
    uint32_t value = 0;
    while (data.size() < size) {
        if (gen() % 4 == 0) {
            static const char* words[] = { "alpha ", "beta ", "gamma ",
                                           "delta ", "epsilon " };
            data += words[gen() % 5];
        } else {
            value += gen() % 64;
            char buf[4];
            memcpy(buf, &value, sizeof(buf));
            data.append(buf, sizeof(buf));
        }
    }
    data.resize(size);
    return data;
}

struct Outcome {
    std::string frame;
    ZL_ErrorCode errorCode = ZL_ErrorCode_no_error;
    std::vector<ZL_ErrorCode> warnings;
    CCTX_MTStats stats{};
};

bool operator==(const Outcome& a, const Outcome& b)
{
    return a.frame == b.frame && a.errorCode == b.errorCode
            && a.warnings == b.warnings;
}

struct Config {
    int nbWorkers;
    unsigned flags;
    bool permissive;
};

std::string toString(const Config& config)
{
    return "nbWorkers=" + std::to_string(config.nbWorkers)
            + " flags=" + std::to_string(config.flags)
            + " permissive=" + std::to_string(config.permissive);
}

Outcome compress(
        ZL_CCtx* cctx,
        const ZL_Compressor* compressor,
        const std::string& src,
        const Config& config)
{
    Outcome outcome;
    EXPECT_FALSE(ZL_isError(ZL_CCtx_refCompressor(cctx, compressor)));
    EXPECT_FALSE(ZL_isError(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));
    EXPECT_FALSE(ZL_isError(
            ZL_CCtx_setParameter(cctx, ZL_CParam_nbWorkers, config.nbWorkers)));
    EXPECT_FALSE(ZL_isError(ZL_CCtx_setParameter(
            cctx,
            ZL_CParam_permissiveCompression,
            config.permissive ? ZL_TernaryParam_enable
                              : ZL_TernaryParam_disable)));
    CCTX_setMTTestingFlags(cctx, config.flags);

    std::string dst(ZL_compressBound(src.size()), '\0');
    ZL_Report const r = ZL_CCtx_compress(
            cctx, dst.data(), dst.size(), src.data(), src.size());
    CCTX_setMTTestingFlags(cctx, 0);
    outcome.stats = CCTX_getMTStats(cctx);
    ZL_Error_Array const warnings = ZL_CCtx_getWarnings(cctx);
    for (size_t n = 0; n < warnings.size; n++) {
        outcome.warnings.push_back(ZL_E_code(warnings.errors[n]));
    }
    if (ZL_isError(r)) {
        outcome.errorCode = ZL_errorCode(r);
        return outcome;
    }
    dst.resize(ZL_validResult(r));
    outcome.frame = dst;

    // Round trip
    std::string decompressed(src.size(), '\0');
    ZL_Report const d = ZL_decompress(
            decompressed.data(), decompressed.size(), dst.data(), dst.size());
    EXPECT_FALSE(ZL_isError(d));
    if (!ZL_isError(d)) {
        EXPECT_EQ(ZL_validResult(d), src.size());
        EXPECT_TRUE(decompressed == src);
    }
    return outcome;
}

Outcome compress(
        const ZL_Compressor* compressor,
        const std::string& src,
        const Config& config)
{
    ZL_CCtx* const cctx = ZL_CCtx_create();
    Outcome outcome     = compress(cctx, compressor, src, config);
    ZL_CCtx_free(cctx);
    return outcome;
}

class MTDeterminismTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        compressor_ = ZL_Compressor_create();
        ASSERT_NE(compressor_, nullptr);
    }

    void TearDown() override
    {
        ZL_Compressor_free(compressor_);
    }

    ZL_GraphID split(
            const std::vector<size_t>& sizes,
            const std::vector<ZL_GraphID>& successors)
    {
        EXPECT_EQ(sizes.size(), successors.size());
        return ZL_Compressor_registerSplitGraph(
                compressor_,
                ZL_Type_serial,
                sizes.data(),
                successors.data(),
                sizes.size());
    }

    /// Fails on inputs whose size is not a multiple of 4
    ZL_GraphID le32FieldLz()
    {
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_,
                ZL_NODE_CONVERT_SERIAL_TO_NUM_LE32,
                ZL_GRAPH_FIELD_LZ);
    }

    void select(ZL_GraphID graph)
    {
        ASSERT_FALSE(ZL_isError(
                ZL_Compressor_selectStartingGraphID(compressor_, graph)));
    }

    /// Compresses @p src in all configurations, and compares with serial.
    /// @returns the serial outcome
    Outcome testAllConfigs(
            const std::string& src,
            bool permissive,
            bool expectOffload)
    {
        Outcome const serial =
                compress(compressor_, src, { 0, 0, permissive });
        EXPECT_EQ(serial.stats.nbSpliced, 0u);
        EXPECT_EQ(serial.stats.nbFallbacks, 0u);
        for (int nbWorkers : { 1, 2, 4, 8 }) {
            for (unsigned flags :
                 { 0u,
                   CCTX_MT_FORCE_OFFLOAD,
                   CCTX_MT_SYNCHRONOUS,
                   CCTX_MT_FORCE_OFFLOAD | CCTX_MT_SYNCHRONOUS }) {
                Config const config = { nbWorkers, flags, permissive };
                if (!ZL_MULTITHREAD && !(flags & CCTX_MT_SYNCHRONOUS)) {
                    continue;
                }
                Outcome const mt = compress(compressor_, src, config);
                EXPECT_TRUE(mt == serial) << toString(config);
                if (nbWorkers > 1 && expectOffload
                    && (flags & CCTX_MT_FORCE_OFFLOAD)) {
                    EXPECT_GT(mt.stats.nbSpliced + mt.stats.nbFallbacks, 0u)
                            << toString(config);
                }
                if (nbWorkers <= 1) {
                    EXPECT_EQ(mt.stats.nbSpliced, 0u) << toString(config);
                }
            }
        }
        return serial;
    }

    ZL_Compressor* compressor_ = nullptr;
};

TEST_F(MTDeterminismTest, MixedSuccessors)
{
    select(split(
            { 150000, 200000, 120000, 180000, 0 },
            { ZL_GRAPH_COMPRESS_GENERIC,
              ZL_GRAPH_ZSTD,
              le32FieldLz(),
              ZL_GRAPH_ENTROPY,
              ZL_GRAPH_COMPRESS_GENERIC }));
    std::string const src = genData(800000, 1);
    Outcome const serial  = testAllConfigs(src, false, true);
    EXPECT_EQ(serial.errorCode, ZL_ErrorCode_no_error);

    // Without forcing, large non-dominant successors are offloaded
    if (ZL_MULTITHREAD) {
        Outcome const mt = compress(compressor_, src, { 4, 0, false });
        EXPECT_GT(mt.stats.nbSpliced, 0u);
        EXPECT_EQ(mt.stats.nbFallbacks, 0u);
    }
}

TEST_F(MTDeterminismTest, ManySmallSuccessors)
{
    // Includes segments below minStreamSize, which are stored directly,
    // and empty segments
    std::vector<size_t> sizes;
    std::vector<ZL_GraphID> successors;
    for (size_t n = 0; n < 40; n++) {
        sizes.push_back((n % 7) * (n % 3 ? 1 : 1000) + (n % 5 == 0 ? 0 : 3));
        successors.push_back(
                n % 2 ? ZL_GRAPH_COMPRESS_GENERIC : ZL_GRAPH_ZSTD);
    }
    sizes.back() = 0;
    select(split(sizes, successors));
    Outcome const serial = testAllConfigs(genData(100000, 2), false, true);
    EXPECT_EQ(serial.errorCode, ZL_ErrorCode_no_error);
}

TEST_F(MTDeterminismTest, ExtraBackends)
{
    select(split(
            { 150000, 150000, 150000, 0 },
            { ZL_GRAPH_DEFLATE,
              ZL_GRAPH_LZMA2,
              ZL_GRAPH_BZIP3,
              ZL_GRAPH_COMPRESS_GENERIC }));
    std::string const src = genData(600000, 7);
    Outcome const serial  = testAllConfigs(src, false, true);
    EXPECT_EQ(serial.errorCode, ZL_ErrorCode_no_error);
}

TEST_F(MTDeterminismTest, SerialBackendSearch)
{
    // Each successor tries all backends with tryGraph(), within its worker
    ASSERT_FALSE(ZL_isError(ZL_Compressor_setParameter(
            compressor_,
            ZL_CParam_serialBackendSearch,
            ZL_SerialBackendSearch_all)));
    select(split(
            { 120000, 120000, 120000, 0 },
            { ZL_GRAPH_COMPRESS_GENERIC,
              ZL_GRAPH_COMPRESS_GENERIC,
              ZL_GRAPH_COMPRESS_GENERIC,
              ZL_GRAPH_COMPRESS_GENERIC }));
    std::string const src = genData(480000, 8);
    Outcome const serial  = testAllConfigs(src, false, true);
    EXPECT_EQ(serial.errorCode, ZL_ErrorCode_no_error);
}

TEST_F(MTDeterminismTest, NestedFanOut)
{
    // The first successor is dominant: it runs on the calling thread,
    // and fans out its own successors
    ZL_GraphID const inner = split(
            { 200000, 200000, 0 },
            { ZL_GRAPH_COMPRESS_GENERIC, le32FieldLz(), ZL_GRAPH_ZSTD });
    ZL_GraphID const other = split(
            { 30000, 0 }, { ZL_GRAPH_ZSTD, ZL_GRAPH_COMPRESS_GENERIC });
    select(split({ 700000, 100000, 0 }, { inner, other, ZL_GRAPH_ZSTD }));
    std::string const src = genData(900000, 3);
    Outcome const serial  = testAllConfigs(src, false, true);
    EXPECT_EQ(serial.errorCode, ZL_ErrorCode_no_error);

    if (ZL_MULTITHREAD) {
        Outcome const mt = compress(compressor_, src, { 4, 0, false });
        EXPECT_GT(mt.stats.nbSpliced, 0u);
    }
}

TEST_F(MTDeterminismTest, PermissiveFailures)
{
    // Segments of odd sizes make le32FieldLz() fail
    select(split(
            { 100001, 150000, 90003, 120000, 0 },
            { le32FieldLz(),
              ZL_GRAPH_COMPRESS_GENERIC,
              le32FieldLz(),
              le32FieldLz(),
              le32FieldLz() }));
    std::string const src = genData(600001, 4);
    Outcome const serial  = testAllConfigs(src, true, true);
    EXPECT_EQ(serial.errorCode, ZL_ErrorCode_no_error);
    EXPECT_GE(serial.warnings.size(), 2u);
}

TEST_F(MTDeterminismTest, StrictFailure)
{
    select(split(
            { 100000, 150000, 90003, 120000, 0 },
            { le32FieldLz(),
              ZL_GRAPH_COMPRESS_GENERIC,
              le32FieldLz(),
              ZL_GRAPH_ZSTD,
              ZL_GRAPH_COMPRESS_GENERIC }));
    std::string const src = genData(600000, 5);
    Outcome const serial  = testAllConfigs(src, false, true);
    EXPECT_NE(serial.errorCode, ZL_ErrorCode_no_error);

    // The failing successor is run again serially
    Outcome const mt = compress(
            compressor_,
            src,
            { 4, CCTX_MT_FORCE_OFFLOAD | CCTX_MT_SYNCHRONOUS, false });
    EXPECT_GT(mt.stats.nbFallbacks, 0u);
}

TEST_F(MTDeterminismTest, ReuseContext)
{
    select(split(
            { 150000, 150000, 150000, 0 },
            { ZL_GRAPH_COMPRESS_GENERIC,
              ZL_GRAPH_ZSTD,
              ZL_GRAPH_COMPRESS_GENERIC,
              ZL_GRAPH_ZSTD }));
    std::string const src = genData(600000, 6);
    Outcome const serial  = compress(compressor_, src, { 0, 0, false });

    ZL_CCtx* const cctx = ZL_CCtx_create();
    for (int nbWorkers : { 4, 2, 0, 8, 8, 3 }) {
        for (unsigned flags : { 0u, CCTX_MT_FORCE_OFFLOAD }) {
            Config const config = { nbWorkers, flags, false };
            Outcome const mt    = compress(cctx, compressor_, src, config);
            EXPECT_TRUE(mt == serial) << toString(config);
        }
    }
    ZL_CCtx_free(cctx);
}

TEST(MTParametersTest, Validation)
{
    ZL_CCtx* const cctx = ZL_CCtx_create();
    EXPECT_FALSE(ZL_isError(ZL_CCtx_setParameter(cctx, ZL_CParam_nbWorkers, 0)));
    EXPECT_FALSE(ZL_isError(ZL_CCtx_setParameter(
            cctx, ZL_CParam_nbWorkers, ZL_NBWORKERS_MAX)));
    EXPECT_EQ(
            ZL_CCtx_getParameter(cctx, ZL_CParam_nbWorkers), ZL_NBWORKERS_MAX);
    EXPECT_TRUE(ZL_isError(ZL_CCtx_setParameter(cctx, ZL_CParam_nbWorkers, -1)));
    EXPECT_TRUE(ZL_isError(ZL_CCtx_setParameter(
            cctx, ZL_CParam_nbWorkers, ZL_NBWORKERS_MAX + 1)));
    EXPECT_FALSE(ZL_isError(
            ZL_CCtx_setParameter(cctx, ZL_CParam_mtMinTaskSize, 1000)));
    EXPECT_EQ(ZL_CCtx_getParameter(cctx, ZL_CParam_mtMinTaskSize), 1000);
    EXPECT_TRUE(ZL_isError(
            ZL_CCtx_setParameter(cctx, ZL_CParam_mtMinTaskSize, -1)));
    ZL_CCtx_free(cctx);
}

} // namespace
