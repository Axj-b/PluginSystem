#pragma once

#include "PipelineSamples.h"

#include <pluginsystem/sdk.hpp>

#include <cstdint>

class PipelinePresenterPlugin final : public pluginsystem::sdk::PluginBase {
public:
    static void Register(pluginsystem::sdk::PluginRegistration<PipelinePresenterPlugin>& api);

    int Start() override;
    int Stop() override;
    void Process();

private:
    pluginsystem::sdk::InputPort<PipelineExample::PipelineFrame> frame_input_{"FrameIn"};
    pluginsystem::sdk::OutputPort<PipelineExample::PipelineFrame> presentation_output_{"Presentation"};
    pluginsystem::sdk::Property<float> warning_threshold_{"WarningThreshold", "Warning Threshold"};

    std::uint32_t presentation_count_{0};
    bool started_{false};
};
