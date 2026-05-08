#pragma once

#include "PipelineSamples.h"

#include <pluginsystem/sdk.hpp>

#include <cstdint>

class PipelineProcessorPlugin final : public pluginsystem::sdk::PluginBase {
public:
    static void Register(pluginsystem::sdk::PluginRegistration<PipelineProcessorPlugin>& api);

    int Start() override;
    int Stop() override;
    void Process();

    bool HasRender() const override { return true; }
    void Render(void* user_context) override;

private:
    pluginsystem::sdk::InputPort<PipelineExample::PipelineFrame> frame_input_{"FrameIn"};
    pluginsystem::sdk::OutputPort<PipelineExample::PipelineFrame> frame_output_{"FrameOut"};
    pluginsystem::sdk::Property<float> gain_{"Gain", "Gain"};
    pluginsystem::sdk::Property<float> offset_{"Offset", "Offset"};
    pluginsystem::sdk::Property<bool> applyOffset_{"ApplyOffset", "Apply Offset"};
    pluginsystem::sdk::Property<double> precision_scale_{"PrecisionScale", "Precision Scale"};
    pluginsystem::sdk::Property<std::int32_t> blend_mode_{"BlendMode", "Blend Mode"};
    pluginsystem::sdk::Property<std::int8_t> exponent_{"Exponent", "Exponent"};
    pluginsystem::sdk::Property<std::uint16_t> max_iterations_{"MaxIterations", "Max Iterations"};

    bool started_{false};
    std::uint64_t last_sequence_{0};
    float last_processed_value_{0.0f};
};
