#pragma once

#include <pluginsystem/sdk/type_name.hpp>

#include <cstdint>

namespace PipelineExample {

struct PipelineFrame {
    std::uint64_t sequence{0};
    float raw_value{0.0F};
    float processed_value{0.0F};
    std::uint32_t presentation_count{0};
    char status[64]{};
};

} // namespace PipelineExample

namespace pluginsystem::sdk {

template <>
struct TypeName<PipelineExample::PipelineFrame> {
    static std::string value()
    {
        return "PipelineExample.PipelineFrame";
    }
};

} // namespace pluginsystem::sdk
