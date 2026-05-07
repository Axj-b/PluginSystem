#include "PipelinePresenterPlugin.h"

#include <cstdio>

void PipelinePresenterPlugin::Register(pluginsystem::sdk::PluginRegistration<PipelinePresenterPlugin>& api)
{
    api.set_plugin(
        "example.pipeline.presenter",
        "Pipeline Presenter",
        "1.0.0",
        "Consumes processed PipelineFrame samples and writes presentation state",
        PS_CONCURRENCY_INSTANCE_SERIALIZED
    );

    api.input(&PipelinePresenterPlugin::frame_input_, pluginsystem::sdk::PortAccessMode::BufferedLatest);
    api.output(&PipelinePresenterPlugin::presentation_output_, pluginsystem::sdk::PortAccessMode::BufferedLatest);
    api.property(&PipelinePresenterPlugin::warning_threshold_, true, true);

    api.entrypoint("Start", &PipelinePresenterPlugin::Start)
        .description("Starts presentation updates");

    api.entrypoint("Stop", &PipelinePresenterPlugin::Stop)
        .description("Stops presentation updates");

    api.entrypoint("Process", &PipelinePresenterPlugin::Process)
        .description("Reads a processed frame and writes presentation state")
        .reads(&PipelinePresenterPlugin::frame_input_)
        .writes(&PipelinePresenterPlugin::presentation_output_)
        .triggeredBy(&PipelinePresenterPlugin::frame_input_);
}

int PipelinePresenterPlugin::Start()
{
    started_ = true;
    presentation_count_ = 0;
    return PS_OK;
}

int PipelinePresenterPlugin::Stop()
{
    started_ = false;
    return PS_OK;
}

void PipelinePresenterPlugin::Process()
{
    auto frame = frame_input_.read();
    if (started_) {
        frame.presentation_count = ++presentation_count_;
        const auto warning_threshold = warning_threshold_.read();
        if (frame.processed_value >= warning_threshold) {
            std::snprintf(frame.status, sizeof(frame.status), "present:warning");
        } else {
            std::snprintf(frame.status, sizeof(frame.status), "present:ok");
        }
    } else {
        std::snprintf(frame.status, sizeof(frame.status), "presenter stopped");
    }

    presentation_output_.write(frame);
}
