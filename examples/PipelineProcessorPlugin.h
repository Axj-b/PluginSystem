#pragma once

#include "PipelineSamples.h"

#include <pluginsystem/sdk.hpp>

class PipelineProcessorPlugin final : public pluginsystem::sdk::PluginBase {
public:
    static void Register(pluginsystem::sdk::PluginRegistration<PipelineProcessorPlugin>& api);

    int Start() override;
    int Stop() override;
    void Process();

private:
    pluginsystem::sdk::InputPort<PipelineExample::PipelineFrame> frame_input_{"FrameIn"};
    pluginsystem::sdk::OutputPort<PipelineExample::PipelineFrame> frame_output_{"FrameOut"};
    pluginsystem::sdk::Property<float> gain_{"Gain", "Gain"};
    pluginsystem::sdk::Property<float> offset_{"Offset", "Offset"};

    bool started_{false};
};
