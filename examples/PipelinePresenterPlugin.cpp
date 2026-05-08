#include "PipelinePresenterPlugin.h"
#include <iostream>
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
    api.property(&PipelinePresenterPlugin::warning_threshold_, true, true, {30.0, -1000.0, 1000.0});
    api.property(&PipelinePresenterPlugin::color_coding_, true, true, {true});
    api.property(&PipelinePresenterPlugin::alert_multiplier_, true, true, {1.5, 0.1, 10.0});
    api.property(&PipelinePresenterPlugin::history_length_, true, true, {1000, 0, 1000000});
    api.property(&PipelinePresenterPlugin::display_count_, true, true, {100, 1, 9999});
    api.property(&PipelinePresenterPlugin::precision_digits_, true, true, {2, 0, 10});
    api.property(&PipelinePresenterPlugin::verbosity_, true, true,
        {1, {}, {}, {"Off", "Normal", "Verbose"}});

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
        const auto warning_threshold = warning_threshold_.read();   // float
        const auto color_coding = color_coding_.read();             // bool
        const auto alert_multiplier = alert_multiplier_.read();     // double
        const auto history_length = history_length_.read();         // int64_t
        const auto display_count = display_count_.read();           // uint32_t
        const auto precision = precision_digits_.read();            // int16_t
        const auto verbosity = verbosity_.read();                   // uint8_t enum: 0=Off, 1=Normal, 2=Verbose

        frame.presentation_count = ++presentation_count_;

        const float effective_threshold = static_cast<float>(warning_threshold * alert_multiplier);
        const bool is_warning = frame.processed_value >= effective_threshold;

        if (verbosity >= 1) {
            std::cout << "Presenter frame " << frame.sequence
                      << ": processed=" << frame.processed_value
                      << " threshold=" << effective_threshold << '\n' << std::flush;
        }
        if (verbosity >= 2) {
            std::cout << "  history_length=" << history_length
                      << " display_count=" << display_count
                      << " precision=" << precision << '\n' << std::flush;
        }

        if (is_warning) {
            if (color_coding) {
                std::snprintf(frame.status, sizeof(frame.status), "**WARNING** pres#%u", presentation_count_);
            } else {
                std::snprintf(frame.status, sizeof(frame.status), "WARNING pres#%u", presentation_count_);
            }
        } else {
            std::snprintf(frame.status, sizeof(frame.status), "ok pres#%u prec=%d hist=%lld",
                presentation_count_, static_cast<int>(precision), static_cast<long long>(history_length));
        }
    } else {
        std::snprintf(frame.status, sizeof(frame.status), "presenter stopped");
    }

    presentation_output_.write(frame);
}
