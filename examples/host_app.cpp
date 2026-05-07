#include <pluginsystem/plugin_manager.hpp>

#include "Samples.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct Sample {
    int32_t value;
};

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t default_iterations = 1000;

struct MetricAccumulator {
    std::chrono::nanoseconds total{};

    void add(std::chrono::nanoseconds elapsed)
    {
        total += elapsed;
    }

    double total_microseconds() const
    {
        return std::chrono::duration<double, std::micro>{total}.count();
    }

    double average_microseconds(std::uint64_t iterations) const
    {
        return total_microseconds() / static_cast<double>(iterations);
    }
};

std::chrono::nanoseconds elapsed_since(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);
}

std::uint64_t parse_iterations(std::string_view text)
{
    std::uint64_t iterations{};
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, iterations);
    if (result.ec != std::errc{} || result.ptr != end || iterations == 0) {
        throw std::invalid_argument{"iterations must be a positive integer"};
    }
    return iterations;
}

void print_usage()
{
    std::cerr << "Usage: host_app <plugin-library-path> [iterations]\n"
              << "  iterations must be a positive integer; default is "
              << default_iterations << ".\n";
}

void print_setup_metric(std::string_view label, std::chrono::nanoseconds elapsed)
{
    std::cout << "  " << label << ": "
              << std::fixed << std::setprecision(3)
              << std::chrono::duration<double, std::micro>{elapsed}.count()
              << " us\n";
}

void print_repeated_metric(std::string_view label, const MetricAccumulator& metric, std::uint64_t iterations)
{
    std::cout << "  " << label << ": total "
              << std::fixed << std::setprecision(1)
              << metric.total_microseconds()
              << " us, avg "
              << metric.average_microseconds(iterations)
              << " us\n";
}

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
    if (argc < 2 || argc > 3) {
        print_usage();
        return 2;
    }

    std::uint64_t iterations = default_iterations;
    if (argc == 3) {
        try {
            iterations = parse_iterations(argv[2]);
        } catch (const std::invalid_argument& error) {
            std::cerr << error.what() << '\n';
            print_usage();
            return 2;
        }
    }

    try {
        pluginsystem::PluginRegistry registry;

        const auto dll_registration_start = Clock::now();
        registry.add_dll_plugin(argv[1]);
        const auto dll_registration_elapsed = elapsed_since(dll_registration_start);

        const auto builtin_registration_start = Clock::now();
        registry.register_builtin(make_builtin_plugin());
        const auto builtin_registration_elapsed = elapsed_since(builtin_registration_start);

        auto discovery_start = Clock::now();
        const auto descriptors = registry.discover_plugins();
        const auto discovery_elapsed = elapsed_since(discovery_start);

        std::cout << "Discovered plugins:\n";
        for (const auto& descriptor : descriptors) {
            std::cout << "  " << descriptor.id << " from " << descriptor.provider_id << '\n';
            if (descriptor.id == "example.greeter") {
                std::cout << "    entrypoints:";
                for (const auto& entrypoint : descriptor.entrypoints) {
                    std::cout << ' ' << entrypoint.id;
                }
                std::cout << "\n    properties:";
                for (const auto& property : descriptor.properties) {
                    std::cout << ' ' << property.id;
                }
                std::cout << '\n';
            }
        }
        std::cout << "Benchmark iterations: " << iterations << '\n';

        pluginsystem::PluginInstanceConfig dll_config;
        dll_config.blueprint_name = "DemoBlueprint";
        dll_config.instance_name = "GreeterDll";
        dll_config.runtime_directory = "pluginsystem_runtime";

        const auto dll_creation_start = Clock::now();
        auto dll_instance = registry.create_instance("example.greeter", dll_config);
        const auto dll_creation_elapsed = elapsed_since(dll_creation_start);

        auto point_cloud = std::make_unique<AutomotiveSensors::PointCloud>();
        point_cloud->timestamp = 123456789;
        point_cloud->sensorId = 7;
        point_cloud->pointCount = 1;
        point_cloud->points[0].x = 10.0F;
        point_cloud->points[0].y = 2.0F;
        point_cloud->points[0].z = 0.5F;
        point_cloud->points[0].intensity = 0.85F;

        auto object_list = std::make_unique<AutomotiveSensors::ObjectList>();
        auto& point_cloud_port = dll_instance->port("PointCloud");
        auto& object_list_output_port = dll_instance->port("ObjectListOutput");
        auto& dll_properties = dll_instance->properties();

        bool has_min_confidence = false;
        for (const auto& slot : dll_properties.slots()) {
            if (slot.descriptor.id == "MinConfidence") {
                has_min_confidence = true;
                break;
            }
        }
        if (!has_min_confidence) {
            throw std::runtime_error{"Expected MinConfidence property was not bound."};
        }

        const auto write_min_confidence = [&dll_properties](float value) {
            dll_properties.write("MinConfidence", &value, sizeof(value));
        };

        float min_confidence = 0.5F;
        write_min_confidence(min_confidence);
        point_cloud_port.write(point_cloud.get(), sizeof(*point_cloud));
        if (dll_instance->invoke("Process") != PS_OK) {
            throw std::runtime_error{"Process invocation failed."};
        }
        object_list_output_port.read(object_list.get(), sizeof(*object_list));
        std::cout << "Pre-start object count: " << object_list->objectCount << '\n';
        if (object_list->objectCount != 0) {
            throw std::runtime_error{"Process produced output before Start was invoked."};
        }

        if (dll_instance->invoke("Start") != PS_OK) {
            throw std::runtime_error{"Start invocation failed."};
        }

        min_confidence = 0.95F;
        write_min_confidence(min_confidence);
        point_cloud_port.write(point_cloud.get(), sizeof(*point_cloud));
        if (dll_instance->invoke("Process") != PS_OK) {
            throw std::runtime_error{"Process invocation failed."};
        }
        object_list_output_port.read(object_list.get(), sizeof(*object_list));
        std::cout << "Filtered object count with MinConfidence "
                  << min_confidence << ": " << object_list->objectCount << '\n';
        if (object_list->objectCount != 0) {
            throw std::runtime_error{"MinConfidence property did not filter the output object."};
        }

        min_confidence = 0.5F;
        write_min_confidence(min_confidence);

        MetricAccumulator dll_pipeline_metric;
        MetricAccumulator dll_write_metric;
        MetricAccumulator dll_invoke_metric;
        MetricAccumulator dll_read_metric;
        for (std::uint64_t index = 0; index < iterations; ++index) {
            const auto pipeline_start = Clock::now();

            auto operation_start = Clock::now();
            point_cloud_port.write(point_cloud.get(), sizeof(*point_cloud));
            dll_write_metric.add(elapsed_since(operation_start));

            operation_start = Clock::now();
            dll_instance->invoke("Process");
            dll_invoke_metric.add(elapsed_since(operation_start));

            operation_start = Clock::now();
            object_list_output_port.read(object_list.get(), sizeof(*object_list));
            dll_read_metric.add(elapsed_since(operation_start));

            dll_pipeline_metric.add(elapsed_since(pipeline_start));
        }

        std::cout << "Process output object count: " << object_list->objectCount << '\n';
        if (object_list->objectCount != 1) {
            throw std::runtime_error{"Process did not produce the expected object after lowering MinConfidence."};
        }
        if (object_list->objectCount > 0) {
            const auto& object = object_list->objects[0];
            std::cout << "First object position: "
                      << object.x << ", " << object.y << ", " << object.z
                      << " confidence=" << object.confidence << '\n';
        }

        if (dll_instance->invoke("Reset") != PS_OK) {
            throw std::runtime_error{"Reset invocation failed."};
        }
        object_list_output_port.read(object_list.get(), sizeof(*object_list));
        std::cout << "Reset output object count: " << object_list->objectCount << '\n';
        if (object_list->objectCount != 0) {
            throw std::runtime_error{"Reset did not clear the output object list."};
        }

        bool unknown_entrypoint_failed = false;
        try {
            (void)dll_instance->invoke("DoesNotExist");
        } catch (const pluginsystem::PluginError&) {
            unknown_entrypoint_failed = true;
        }
        if (!unknown_entrypoint_failed) {
            throw std::runtime_error{"Unknown entrypoint did not fail."};
        }

        if (dll_instance->invoke("Stop") != PS_OK) {
            throw std::runtime_error{"Stop invocation failed."};
        }

        pluginsystem::PluginInstanceConfig builtin_config;
        builtin_config.blueprint_name = "DemoBlueprint";
        builtin_config.instance_name = "BuiltinIncrement";

        const auto builtin_creation_start = Clock::now();
        auto builtin_instance = registry.create_instance("example.builtin.increment", builtin_config);
        const auto builtin_creation_elapsed = elapsed_since(builtin_creation_start);

        Sample builtin_input{41};
        Sample builtin_output{};
        auto& builtin_input_port = builtin_instance->port("input");
        auto& builtin_output_port = builtin_instance->port("output");

        MetricAccumulator builtin_pipeline_metric;
        for (std::uint64_t index = 0; index < iterations; ++index) {
            const auto pipeline_start = Clock::now();
            builtin_input_port.write(&builtin_input, sizeof(builtin_input));
            builtin_instance->invoke("Increment");
            builtin_output_port.read(&builtin_output, sizeof(builtin_output));
            builtin_pipeline_metric.add(elapsed_since(pipeline_start));
        }

        std::cout << "Built-in output: " << builtin_output.value << '\n';

        std::cout << "\nSetup timings:\n";
        print_setup_metric("DLL provider registration", dll_registration_elapsed);
        print_setup_metric("Built-in provider registration", builtin_registration_elapsed);
        print_setup_metric("Registry discovery", discovery_elapsed);
        print_setup_metric("DLL instance creation and binding", dll_creation_elapsed);
        print_setup_metric("Built-in instance creation and binding", builtin_creation_elapsed);

        std::cout << "\nDLL Process pipeline timings:\n";
        print_repeated_metric("PointCloud write", dll_write_metric, iterations);
        print_repeated_metric("invoke(\"Process\")", dll_invoke_metric, iterations);
        print_repeated_metric("ObjectListOutput read", dll_read_metric, iterations);
        print_repeated_metric("full iteration", dll_pipeline_metric, iterations);

        std::cout << "\nBuilt-in Increment baseline timings:\n";
        print_repeated_metric("full iteration", builtin_pipeline_metric, iterations);
    } catch (const pluginsystem::PluginError& error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
