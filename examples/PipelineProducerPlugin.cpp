#include "PipelineProducerPlugin.h"

#include <cstdio>

void PipelineProducerPlugin::Register(pluginsystem::sdk::PluginRegistration<PipelineProducerPlugin>& api)
{
    api.set_plugin(
        "example.pipeline.producer",
        "Pipeline Producer",
        "1.0.0",
        "Produces PipelineFrame samples for graph runtime examples",
        PS_CONCURRENCY_INSTANCE_SERIALIZED
    );

    api.output(&PipelineProducerPlugin::frame_output_, pluginsystem::sdk::PortAccessMode::BufferedLatest);
    api.property(&PipelineProducerPlugin::start_value_, true, true);
    api.property(&PipelineProducerPlugin::step_, true, true);

    api.entrypoint("Start", &PipelineProducerPlugin::Start)
        .description("Starts sample production");

    api.entrypoint("Stop", &PipelineProducerPlugin::Stop)
        .description("Stops sample production");

    api.entrypoint("Process", &PipelineProducerPlugin::Process)
        .description("Produces one PipelineFrame sample")
        .writes(&PipelineProducerPlugin::frame_output_);
}

int PipelineProducerPlugin::Start()
{
    started_ = true;
    next_sequence_ = 0;
    return PS_OK;
}

int PipelineProducerPlugin::Stop()
{
    started_ = false;
    return PS_OK;
}

void PipelineProducerPlugin::Process()
{
    PipelineExample::PipelineFrame frame{};
    frame.sequence = next_sequence_++;

    if (started_) {
        const auto start_value = start_value_.read();
        const auto step = step_.read();
        frame.raw_value = start_value + static_cast<float>(frame.sequence) * step;
        frame.processed_value = frame.raw_value;
        std::snprintf(frame.status, sizeof(frame.status), "produced");
    } else {
        std::snprintf(frame.status, sizeof(frame.status), "producer stopped");
    }

    frame_output_.write(frame);
}
