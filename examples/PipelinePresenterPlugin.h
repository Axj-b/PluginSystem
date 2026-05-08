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
    pluginsystem::sdk::Property<bool> color_coding_{"ColorCoding", "Color Coding"};
    pluginsystem::sdk::Property<double> alert_multiplier_{"AlertMultiplier", "Alert Multiplier"};
    pluginsystem::sdk::Property<std::int64_t> history_length_{"HistoryLength", "History Length"};
    pluginsystem::sdk::Property<std::uint32_t> display_count_{"DisplayCount", "Display Count"};
    pluginsystem::sdk::Property<std::int16_t> precision_digits_{"PrecisionDigits", "Precision Digits"};
    pluginsystem::sdk::Property<std::uint8_t> verbosity_{"Verbosity", "Verbosity"};

    std::uint32_t presentation_count_{0};
    bool started_{false};
};
