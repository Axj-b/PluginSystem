#pragma once

#include "PipelineSamples.h"

#include <pluginsystem/sdk.hpp>

#include <cstdint>

class PipelineProducerPlugin final : public pluginsystem::sdk::PluginBase {
public:
    static void Register(pluginsystem::sdk::PluginRegistration<PipelineProducerPlugin>& api);

    int Start() override;
    int Stop() override;
    void Process();

private:
    pluginsystem::sdk::OutputPort<PipelineExample::PipelineFrame> frame_output_{"Frame"};
    pluginsystem::sdk::Property<float> start_value_{"StartValue", "Start Value"};
    pluginsystem::sdk::Property<float> step_{"Step", "Step"};
    pluginsystem::sdk::Property<std::uint64_t> id_offset_{"IdOffset", "ID Offset"};

    std::uint64_t next_sequence_{0};
    bool started_{false};
};
