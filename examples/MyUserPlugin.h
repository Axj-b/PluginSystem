#pragma once

#include "Samples.h"

#include <pluginsystem/sdk.hpp>

#include <memory>

class MyUserPlugin final : public pluginsystem::sdk::PluginBase {
public:
    static void Register(pluginsystem::sdk::PluginRegistration<MyUserPlugin>& api);

    int Init() override;
    int Start() override;
    int Stop() override;
    void Process();
    void Reset();

private:
    pluginsystem::sdk::InputPort<AutomotiveSensors::PointCloud> point_cloud_input_{"PointCloud"};
    pluginsystem::sdk::OutputPort<AutomotiveSensors::ObjectList> object_list_output_{"ObjectListOutput"};
    pluginsystem::sdk::Property<float> min_confidence_{"MinConfidence", "Minimum Confidence"};

    std::unique_ptr<AutomotiveSensors::PointCloud> point_cloud_;
    std::unique_ptr<AutomotiveSensors::ObjectList> object_list_;
    bool started_{false};
};
