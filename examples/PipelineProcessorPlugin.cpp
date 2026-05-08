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
    api.property(&PipelineProcessorPlugin::gain_, true, true);
    api.property(&PipelineProcessorPlugin::offset_, true, true);

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
        frame.processed_value = frame.raw_value * gain + offset;
        std::snprintf(frame.status, sizeof(frame.status), "processed");
        std::cout<< "Processed frame " << frame.sequence << ": raw=" << frame.raw_value << " processed=" << frame.processed_value << '\n' << std::flush;
    } else {
        std::snprintf(frame.status, sizeof(frame.status), "processor stopped");
    }

    frame_output_.write(frame);
}

