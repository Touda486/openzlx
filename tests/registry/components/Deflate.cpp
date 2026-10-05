// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Deflate.hpp"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {
class DeflateComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Deflate";
    }

    int minFormatVersion() const override
    {
        return 28;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        graphs.push_back(graphs::Deflate{}(compressor));
        graphs.push_back(graphs::Deflate{ 1 }(compressor));
        graphs.push_back(graphs::Deflate{ 9 }(compressor));
        // Out of range levels are clamped
        graphs.push_back(graphs::Deflate{ -3 }(compressor));
        graphs.push_back(graphs::Deflate{ 22 }(compressor));
        return graphs;
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        graphs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            graphs.push_back(graphs::Deflate{
                    gen.i32_range("compression_level", -5, 12) }(compressor));
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(std::make_unique<SerialOpenZLInput>(""));
        inputs.push_back(std::make_unique<SerialOpenZLInput>("x"));
        inputs.push_back(std::make_unique<SerialOpenZLInput>("xy"));
        inputs.push_back(std::make_unique<SerialOpenZLInput>("abc"));
        inputs.push_back(std::make_unique<SerialOpenZLInput>("abcde"));
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(std::string(1000, 'x')));
        inputs.push_back(std::make_unique<SerialOpenZLInput>(kLoremTestInput));
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(kAudioPCMS32LETestInput));
        return inputs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor&,
            GraphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            datagen::CompressibleStringProducer producer(
                    gen.getRandWrapper(),
                    gen.usize_range("input_size", 0, maxInputSize),
                    gen.u32_range("match_prob", 0, 100) / 100.0);
            inputs.push_back(
                    std::make_unique<SerialOpenZLInput>(producer("input")));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeDeflateComponent()
{
    return std::make_unique<DeflateComponent>();
}
} // namespace openzl::tests::components
