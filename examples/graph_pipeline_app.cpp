#include <pluginsystem/plugin_manager.hpp>
#include <dll_plugin_provider.hpp>

#include "PipelineSamples.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uint64_t default_iterations = 5;

void print_usage()
{
    std::cerr << "Usage: graph_pipeline_app <producer-plugin.dll> <processor-plugin.dll> <presenter-plugin.dll> [iterations]\n"
              << "  iterations must be a positive integer; default is " << default_iterations << ".\n";
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

void write_float_property(pluginsystem::GraphRuntime& graph, std::string_view node_id, std::string_view property_id, float value)
{
    graph.properties(node_id).write(property_id, &value, sizeof(value));
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4 || argc > 5) {
        print_usage();
        return 2;
    }

    std::uint64_t iterations = default_iterations;
    if (argc == 5) {
        try {
            iterations = parse_iterations(argv[4]);
        } catch (const std::invalid_argument& error) {
            std::cerr << error.what() << '\n';
            print_usage();
            return 2;
        }
    }

    try {
        pluginsystem::PluginRegistry registry;
        registry.add_provider(std::make_unique<pluginsystem::DllPluginProvider>(argv[1]));
        registry.add_provider(std::make_unique<pluginsystem::DllPluginProvider>(argv[2]));
        registry.add_provider(std::make_unique<pluginsystem::DllPluginProvider>(argv[3]));

        const auto descriptors = registry.discover_plugins();
        std::cout << "Discovered graph plugins:\n";
        for (const auto& descriptor : descriptors) {
            std::cout << "  " << descriptor.id << '\n';
        }

        pluginsystem::GraphConfig config;
        config.blueprint_name = "PipelineGraphDemo";
        config.runtime_directory = "pluginsystem_runtime";
        config.worker_count = 1;
        config.nodes.push_back(pluginsystem::GraphNodeConfig{
            "producer",
            "example.pipeline.producer",
            "Producer",
        });
        config.nodes.push_back(pluginsystem::GraphNodeConfig{
            "processor",
            "example.pipeline.processor",
            "Processor",
        });
        config.nodes.push_back(pluginsystem::GraphNodeConfig{
            "presenter",
            "example.pipeline.presenter",
            "Presenter",
        });
        config.edges.push_back(pluginsystem::GraphEdgeConfig{
            "producer",
            "Frame",
            "processor",
            "FrameIn",
        });
        config.edges.push_back(pluginsystem::GraphEdgeConfig{
            "processor",
            "FrameOut",
            "presenter",
            "FrameIn",
        });

        auto graph = registry.create_graph(std::move(config));

        std::cout << "\nConnected shared-memory channels:\n";
        std::cout << "  producer.Frame -> processor.FrameIn: "
                  << graph->port("producer", "Frame").name() << '\n';
        std::cout << "  processor.FrameOut -> presenter.FrameIn: "
                  << graph->port("processor", "FrameOut").name() << '\n';

        write_float_property(*graph, "producer", "StartValue", 10.0F);
        write_float_property(*graph, "producer", "Step", 1.5F);
        write_float_property(*graph, "processor", "Gain", 2.0F);
        write_float_property(*graph, "processor", "Offset", 3.0F);
        write_float_property(*graph, "presenter", "WarningThreshold", 30.0F);

        std::cout << "\nPipeline output:\n";
        for (std::uint64_t index = 0; index < iterations; ++index) {
            const auto job = graph->submit_run();
            const auto result = graph->wait(job);
            if (result.result != PS_OK) {
                throw std::runtime_error{"Graph run failed at node: " + result.failed_node_id};
            }

            PipelineExample::PipelineFrame frame{};
            graph->port("presenter", "Presentation").read(&frame, sizeof(frame));

            std::cout << "  #" << frame.sequence
                      << " raw=" << frame.raw_value
                      << " processed=" << frame.processed_value
                      << " presentations=" << frame.presentation_count
                      << " status=" << frame.status << '\n';
        }

        graph->stop();
    } catch (const pluginsystem::PluginError& error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
