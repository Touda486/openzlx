// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <climits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cpp/tests/TestUtils.hpp"
#include "openzl/openzl.hpp"

using namespace ::testing;
namespace openzl {
namespace {

std::vector<std::pair<int, int>> collectIntParams(const LocalParams& params)
{
    std::vector<std::pair<int, int>> result;
    for (const auto& param : params.getIntParams()) {
        result.emplace_back(param.paramId, param.paramValue);
    }
    return result;
}

} // namespace

class TestCodecs : public testing::Test {
   public:
    void SetUp() override
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor_.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    }

    Compressor compressor_;
};

TEST_F(TestCodecs, bitpack)
{
    std::vector<int> data(10000, 7);
    data.push_back(0);
    const size_t bound = (data.size() * 3) / 8 + 100;
    compressor_.selectStartingGraph(graphs::Bitpack{}());
    auto compressed = testRoundTrip(
            compressor_, Input::refNumeric(poly::span<const int>{ data }));
    EXPECT_LE(compressed.size(), bound);
}

TEST_F(TestCodecs, lz4)
{
    std::string data(10000, 'a');
    auto graph = graphs::Lz4().parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, lz4_hc)
{
    std::string data(10000, 'a');
    auto graph = graphs::Lz4(9).parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, deflate)
{
    std::string data(10000, 'a');
    for (auto level : { 1, 9 }) {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.selectStartingGraph(
                graphs::Deflate(level).parameterize(compressor));
        testRoundTrip(compressor, Input::refSerial(data));
    }
}

TEST_F(TestCodecs, lzma2)
{
    std::string data(10000, 'a');
    for (auto level : { 0, 9 }) {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.selectStartingGraph(
                graphs::Lzma2(level).parameterize(compressor));
        testRoundTrip(compressor, Input::refSerial(data));
    }
}

TEST_F(TestCodecs, bzip3)
{
    // 2 * 65 KiB: a multiple of the block size, which libbzip3 mishandles
    std::string data(2 * (65 << 10), 'a');
    for (auto blockSize : { 65 << 10, 1 << 20 }) {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.selectStartingGraph(
                graphs::Bzip3(blockSize).parameterize(compressor));
        testRoundTrip(compressor, Input::refSerial(data));
    }
}

TEST_F(TestCodecs, newBackendsRejectedBeforeFormatVersion28)
{
    std::string data(10000, 'a');
    for (auto graph : { ZL_GRAPH_DEFLATE, ZL_GRAPH_LZMA2, ZL_GRAPH_BZIP3 }) {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, 27);
        compressor.selectStartingGraph(graph);
        CCtx cctx;
        cctx.refCompressor(compressor);
        EXPECT_ANY_THROW(cctx.compressSerial(data));
    }
}

TEST_F(TestCodecs, serialBackendSearch)
{
    // Text-like data, on which lzma2 & bzip3 beat zstd
    std::string data;
    for (int i = 0; i < 20000; ++i) {
        data += "line " + std::to_string((i * 7919) % 1013) + " of the test\n";
    }
    auto compressedSize = [&](int search, int formatVersion) {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, formatVersion);
        compressor.setParameter(CParam::SerialBackendSearch, search);
        compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
        return testRoundTrip(compressor, Input::refSerial(data)).size();
    };
    const size_t zstdOnly = compressedSize(0, ZL_MAX_FORMAT_VERSION);
    size_t all            = zstdOnly;
    for (int search = 1; search <= ZL_SerialBackendSearch_all; ++search) {
        const size_t size = compressedSize(search, ZL_MAX_FORMAT_VERSION);
        EXPECT_LE(size, zstdOnly) << "search=" << search;
        if (search == ZL_SerialBackendSearch_all) {
            all = size;
        }
    }
    EXPECT_LT(all, zstdOnly);
    // Older format versions can't use the new backends, and fall back to zstd
    EXPECT_EQ(compressedSize(ZL_SerialBackendSearch_all, 27), compressedSize(0, 27));
    // Invalid values are rejected
    Compressor compressor;
    EXPECT_ANY_THROW(compressor.setParameter(CParam::SerialBackendSearch, 8));
    EXPECT_ANY_THROW(compressor.setParameter(CParam::SerialBackendSearch, -1));
}

TEST_F(TestCodecs, lzParameters)
{
    const auto muxLengthsGraph =
            nodes::MuxLengths{}(compressor_, graphs::Store::graph);
    const graphs::Lz lz(
            graphs::Lz::Parameters{
                    .nodeParams =
                            graphs::Lz::NodeParams{
                                    .compressionLevel = -1,
                                    .acceleration     = 2,
                                    .windowLog        = 16,
                                    .strategy         = ZL_LzStrategy_fast,
                                    .hashLog1         = 15,
                                    .hashLog2         = 17,
                                    .hashLength       = 5,
                                    .searchLog        = 3,
                            },
                    .literalsGraph          = graphs::Store::graph,
                    .offsetsGraph           = graphs::Store::graph,
                    .muxedBytesGraph        = graphs::Store::graph,
                    .overflowLengthsGraph   = graphs::Store::graph,
                    .muxLengthsGraph        = muxLengthsGraph,
                    .minGainForEntropyBytes = 42,
                    .minGainForEntropyPct   = 43,
            });

    const auto graphParameters = lz.parameters();
    ASSERT_TRUE(graphParameters.has_value());
    ASSERT_TRUE(graphParameters->localParams.has_value());
    ASSERT_TRUE(graphParameters->customGraphs.has_value());

    const std::vector<std::pair<int, int>> expectedIntParams{
        { ZL_LzParam_compressionLevel, -1 },
        { ZL_LzParam_acceleration, 2 },
        { ZL_LzParam_windowLog, 16 },
        { ZL_LzParam_strategy, ZL_LzStrategy_fast },
        { ZL_LzParam_hashLog1, 15 },
        { ZL_LzParam_hashLog2, 17 },
        { ZL_LzParam_hashLength, 5 },
        { ZL_LzParam_searchLog, 3 },
        { ZL_LzParam_literalsGraphIdx, 0 },
        { ZL_LzParam_offsetsGraphIdx, 1 },
        { ZL_LzParam_muxedBytesGraphIdx, 2 },
        { ZL_LzParam_overflowLengthsGraphIdx, 3 },
        { ZL_LzParam_muxLengthsGraphIdx, 4 },
        { ZL_LzParam_minGainForEntropyBytes, 42 },
        { ZL_LzParam_minGainForEntropyPct, 43 },
    };
    EXPECT_EQ(
            collectIntParams(*graphParameters->localParams), expectedIntParams);

    const std::vector<GraphID> expectedCustomGraphs{
        graphs::Store::graph, graphs::Store::graph, graphs::Store::graph,
        graphs::Store::graph, muxLengthsGraph,
    };
    EXPECT_EQ(*graphParameters->customGraphs, expectedCustomGraphs);

    std::string data(10000, 'a');
    auto graph = lz.parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, lzNodeParameters)
{
    const nodes::Lz lz(
            nodes::Lz::Parameters{
                    .compressionLevel = 3,
                    .acceleration     = 4,
                    .windowLog        = 17,
                    .strategy         = ZL_LzStrategy_lazy2,
                    .hashLog1         = 14,
                    .hashLog2         = 16,
                    .hashLength       = 6,
                    .searchLog        = 4,
            });

    const auto nodeParameters = lz.parameters();
    ASSERT_TRUE(nodeParameters.has_value());
    ASSERT_TRUE(nodeParameters->localParams.has_value());

    const std::vector<std::pair<int, int>> expectedIntParams{
        { ZL_LzParam_compressionLevel, 3 },
        { ZL_LzParam_acceleration, 4 },
        { ZL_LzParam_windowLog, 17 },
        { ZL_LzParam_strategy, ZL_LzStrategy_lazy2 },
        { ZL_LzParam_hashLog1, 14 },
        { ZL_LzParam_hashLog2, 16 },
        { ZL_LzParam_hashLength, 6 },
        { ZL_LzParam_searchLog, 4 },
    };
    EXPECT_EQ(
            collectIntParams(*nodeParameters->localParams), expectedIntParams);
}

TEST_F(TestCodecs, segmentSerial_defaultChunkSize)
{
    /* chunkByteSize = 0 sentinel: use the segmenter's built-in default. */
    std::string data(4096, 'a');
    auto graph = graphs::SegmentSerial(ZL_GRAPH_COMPRESS_GENERIC)
                         .parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, segmentSerial_explicitChunkSize)
{
    /* Explicit chunk size at the minimum threshold must round-trip. */
    std::string data(ZL_MIN_CHUNK_SIZE * 2, 'a');
    auto graph =
            graphs::SegmentSerial(ZL_GRAPH_COMPRESS_GENERIC, ZL_MIN_CHUNK_SIZE)
                    .parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, segmentSerial_chunkSizeOverflowThrows)
{
    /* The C builder rejects chunk sizes that would not fit in int; the C++
     * wrapper surfaces that rejection as a typed Exception at parameterize
     * time (construction itself does not validate). */
    graphs::SegmentSerial wrapper(
            ZL_GRAPH_COMPRESS_GENERIC, static_cast<size_t>(INT_MAX) + 1);
    EXPECT_THROW(wrapper.parameterize(compressor_), Exception);
}
} // namespace openzl
