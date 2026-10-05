// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_lzma2.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {
struct Lzma2 : public Graph {
   public:
    static constexpr GraphID graph = ZL_GRAPH_LZMA2;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description = "LZMA2 (xz) compress the input data with liblzma",
    };

    Lzma2() {}
    explicit Lzma2(int compressionLevel) : compressionLevel_(compressionLevel) {}

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        LocalParams lp;
        if (compressionLevel_.has_value()) {
            lp.addIntParam(
                    ZL_LZMA2_COMPRESSION_LEVEL_OVERRIDE_PID,
                    compressionLevel_.value());
        }
        return GraphParameters{ .localParams = std::move(lp) };
    }

    ~Lzma2() override = default;

   private:
    poly::optional<int> compressionLevel_;
};
} // namespace graphs
} // namespace openzl
