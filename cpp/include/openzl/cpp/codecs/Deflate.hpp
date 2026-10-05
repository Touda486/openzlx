// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_deflate.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {
struct Deflate : public Graph {
   public:
    static constexpr GraphID graph = ZL_GRAPH_DEFLATE;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description = "Deflate (zip / gzip) compress the input data with zlib",
    };

    Deflate() {}
    explicit Deflate(int compressionLevel) : compressionLevel_(compressionLevel) {}

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        LocalParams lp;
        if (compressionLevel_.has_value()) {
            lp.addIntParam(
                    ZL_DEFLATE_COMPRESSION_LEVEL_OVERRIDE_PID,
                    compressionLevel_.value());
        }
        return GraphParameters{ .localParams = std::move(lp) };
    }

    ~Deflate() override = default;

   private:
    poly::optional<int> compressionLevel_;
};
} // namespace graphs
} // namespace openzl
