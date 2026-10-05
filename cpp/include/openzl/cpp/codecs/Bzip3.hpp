// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_bzip3.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {
struct Bzip3 : public Graph {
   public:
    static constexpr GraphID graph = ZL_GRAPH_BZIP3;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description = "Bzip3 compress the input data",
    };

    Bzip3() {}
    explicit Bzip3(int blockSize) : blockSize_(blockSize) {}

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        LocalParams lp;
        if (blockSize_.has_value()) {
            lp.addIntParam(
                    ZL_BZIP3_BLOCK_SIZE_PID,
                    blockSize_.value());
        }
        return GraphParameters{ .localParams = std::move(lp) };
    }

    ~Bzip3() override = default;

   private:
    poly::optional<int> blockSize_;
};
} // namespace graphs
} // namespace openzl
