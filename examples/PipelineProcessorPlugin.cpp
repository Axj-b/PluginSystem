#include "PipelineProcessorPlugin.h"

#include <cstdio>
#include <iostream>

void PipelineProcessorPlugin::Register(pluginsystem::sdk::PluginRegistration<PipelineProcessorPlugin>& api)
{
    api.set_plugin(
        "example.pipeline.processor",
        "Pipeline Processor",
        "1.0.0",
        "Transforms PipelineFrame samples for graph runtime examples",
        PS_CONCURRENCY_INSTANCE_SERIALIZED
    );

    api.input(&PipelineProcessorPlugin::frame_input_, pluginsystem::sdk::PortAccessMode::BufferedLatest);
    api.output(&PipelineProcessorPlugin::frame_output_, pluginsystem::sdk::PortAccessMode::BufferedLatest);
    api.property(&PipelineProcessorPlugin::gain_, true, true, {1.0, 0.0, 10.0});
    api.property(&PipelineProcessorPlugin::offset_, true, true, {0.0, -100.0, 100.0});
    api.property(&PipelineProcessorPlugin::applyOffset_, true, true, {true});
    api.property(&PipelineProcessorPlugin::precision_scale_, true, true, {1.0, 0.001, 1000.0});
    api.property(&PipelineProcessorPlugin::blend_mode_, true, true,
        {0, {}, {}, {"Add", "Multiply", "Invert"}});
    api.property(&PipelineProcessorPlugin::exponent_, true, true, {0, -8, 8});
    api.property(&PipelineProcessorPlugin::max_iterations_, true, true, {1, 1, 100});

    api.entrypoint("Start", &PipelineProcessorPlugin::Start)
        .description("Starts sample processing");

    api.entrypoint("Stop", &PipelineProcessorPlugin::Stop)
        .description("Stops sample processing");

    api.entrypoint("Process", &PipelineProcessorPlugin::Process)
        .description("Reads one PipelineFrame and writes the processed frame")
        .reads(&PipelineProcessorPlugin::frame_input_)
        .writes(&PipelineProcessorPlugin::frame_output_)
        .triggeredBy(&PipelineProcessorPlugin::frame_input_);
}

int PipelineProcessorPlugin::Start()
{
    started_ = true;
    return PS_OK;
}

int PipelineProcessorPlugin::Stop()
{
    started_ = false;
    return PS_OK;
}

void PipelineProcessorPlugin::Process()
{
    auto frame = frame_input_.read();

    if (started_) {
        const auto gain = gain_.read();
        const auto offset = offset_.read();
        const auto apply_offset = applyOffset_.read();
        const auto precision_scale = precision_scale_.read();   // double
        const auto blend_mode = blend_mode_.read();             // int32_t enum: 0=Add, 1=Multiply, 2=Invert
        const auto exponent = exponent_.read();                 // int8_t
        const auto max_iterations = max_iterations_.read();     // uint16_t

        double result = static_cast<double>(frame.raw_value) * gain * precision_scale;
        if (apply_offset) result += offset;

        switch (blend_mode) {
        case 1: result *= gain; break;
        case 2: result = -result; break;
        default: break;
        }

        result += static_cast<int>(exponent) * 0.001 * result;

        for (std::uint16_t i = 1; i < max_iterations; ++i) {
            result += 0.000001 * result;
        }

        frame.processed_value = static_cast<float>(result);
        std::snprintf(frame.status, sizeof(frame.status), "blend=%d exp=%d iters=%u",
            static_cast<int>(blend_mode), static_cast<int>(exponent), static_cast<unsigned>(max_iterations));
        std::cout << "Processed frame " << frame.sequence << ": " << frame.processed_value << '\n' << std::flush;
    } else {
        std::snprintf(frame.status, sizeof(frame.status), "processor stopped");
    }

    frame_output_.write(frame);
}

