#include <pluginsystem/plugin_manager.hpp>

#include "Samples.h"

#include <iostream>
#include <memory>
#include <utility>

namespace {

struct Sample {
    int32_t value;
};

pluginsystem::BuiltinPluginDefinition make_builtin_plugin()
{
    pluginsystem::PluginDescriptor descriptor;
    descriptor.id = "example.builtin.increment";
    descriptor.name = "Built-in Increment Plugin";
    descriptor.version = "1.0.0";
    descriptor.description = "Built-in source-code plugin registered by the host";
    descriptor.concurrency = pluginsystem::ConcurrencyPolicy::fully_concurrent;
    descriptor.entrypoints.push_back(pluginsystem::EntrypointDescriptor{
        "Increment",
        "Increment",
        "Adds one to the input and writes the output",
        pluginsystem::ConcurrencyPolicy::fully_concurrent,
        {"input"},
        {"output"},
    });
    descriptor.ports.push_back(pluginsystem::PortDescriptor{
        "input",
        "Input",
        pluginsystem::PortDirection::input,
        pluginsystem::PortAccessMode::direct_block,
        sizeof(Sample),
        alignof(Sample),
        "example.Sample",
    });
    descriptor.ports.push_back(pluginsystem::PortDescriptor{
        "output",
        "Output",
        pluginsystem::PortDirection::output,
        pluginsystem::PortAccessMode::buffered_latest,
        sizeof(Sample),
        alignof(Sample),
        "example.Sample",
    });

    pluginsystem::BuiltinPluginDefinition definition;
    definition.descriptor = std::move(descriptor);
    definition.factory = [](const pluginsystem::PluginDescriptor&, const pluginsystem::PluginInstanceConfig&, const pluginsystem::RuntimeBindings&) {
        return std::make_unique<pluginsystem::BuiltinPluginInstanceBackend>(
            [](std::string_view entrypoint, pluginsystem::InvocationContext& context) {
                if (entrypoint != "Increment") {
                    return static_cast<int32_t>(PS_NOT_FOUND);
                }

                Sample input{};
                context.read_port("input", &input, sizeof(input));
                Sample output{input.value + 1};
                context.write_port("output", &output, sizeof(output));
                return static_cast<int32_t>(PS_OK);
            }
        );
    };
    return definition;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: host_app <plugin-library-path>\n";
        return 2;
    }

    try {
        pluginsystem::PluginRegistry registry;
        registry.add_dll_plugin(argv[1]);
        registry.register_builtin(make_builtin_plugin());

        const auto descriptors = registry.discover_plugins();
        std::cout << "Discovered plugins:\n";
        for (const auto& descriptor : descriptors) {
            std::cout << "  " << descriptor.id << " from " << descriptor.provider_id << '\n';
        }

        pluginsystem::PluginInstanceConfig dll_config;
        dll_config.blueprint_name = "DemoBlueprint";
        dll_config.instance_name = "GreeterDll";
        dll_config.runtime_directory = "pluginsystem_runtime";

        auto dll_instance = registry.create_instance("example.greeter", dll_config);

        auto point_cloud = std::make_unique<AutomotiveSensors::PointCloud>();
        point_cloud->timestamp = 123456789;
        point_cloud->sensorId = 7;
        point_cloud->pointCount = 1;
        point_cloud->points[0].x = 10.0F;
        point_cloud->points[0].y = 2.0F;
        point_cloud->points[0].z = 0.5F;
        point_cloud->points[0].intensity = 0.85F;

        dll_instance->port("PointCloud").write(point_cloud.get(), sizeof(*point_cloud));
        dll_instance->invoke("Process");

        auto object_list = std::make_unique<AutomotiveSensors::ObjectList>();
        dll_instance->port("ObjectListOutput").read(object_list.get(), sizeof(*object_list));
        std::cout << "Process output object count: " << object_list->objectCount << '\n';
        if (object_list->objectCount > 0) {
            const auto& object = object_list->objects[0];
            std::cout << "First object position: "
                      << object.x << ", " << object.y << ", " << object.z
                      << " confidence=" << object.confidence << '\n';
        }

        pluginsystem::PluginInstanceConfig builtin_config;
        builtin_config.blueprint_name = "DemoBlueprint";
        builtin_config.instance_name = "BuiltinIncrement";
        auto builtin_instance = registry.create_instance("example.builtin.increment", builtin_config);

        Sample builtin_input{41};
        builtin_instance->port("input").write(&builtin_input, sizeof(builtin_input));
        builtin_instance->invoke("Increment");
        Sample builtin_output{};
        builtin_instance->port("output").read(&builtin_output, sizeof(builtin_output));
        std::cout << "Built-in output: " << builtin_output.value << '\n';
    } catch (const pluginsystem::PluginError& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
