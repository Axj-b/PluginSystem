#pragma once

#include "IPlugin.h"
#include "Samples.h"

#include <memory>

class MyUserPlugin final : public ExamplePluginSdk::IPlugin {
public:
    static void Register(ExamplePluginSdk::PluginRegistration<MyUserPlugin>& api);

    int Init() override;
    int Start() override;
    int Stop() override;
    void* GetRenderer() override;
    void Process() override;

private:
    ExamplePluginSdk::InputPort<AutomotiveSensors::PointCloud> point_cloud_input_{"PointCloud"};
    ExamplePluginSdk::OutputPort<AutomotiveSensors::ObjectList> object_list_output_{"ObjectListOutput"};

    std::unique_ptr<AutomotiveSensors::PointCloud> point_cloud_;
    std::unique_ptr<AutomotiveSensors::ObjectList> object_list_;
    bool started_{false};
};
