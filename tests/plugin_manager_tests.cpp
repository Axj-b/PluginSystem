#include <pluginsystem/plugin_manager.hpp>

#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <thread>
#include <vector>

namespace {

struct Sample {
    int32_t value;
};

pluginsystem::PluginDescriptor make_builtin_descriptor(
    std::string id,
    std::string entrypoint_id,
    pluginsystem::ConcurrencyPolicy policy
)
{
    pluginsystem::PluginDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.name = descriptor.id;
    descriptor.version = "1.0.0";
    descriptor.description = "Built-in test plugin";
    descriptor.concurrency = policy;
    descriptor.entrypoints.push_back(pluginsystem::EntrypointDescriptor{
        entrypoint_id,
        entrypoint_id,
        "Built-in test entrypoint",
        policy,
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
        "test.Sample",
    });
    descriptor.ports.push_back(pluginsystem::PortDescriptor{
        "output",
        "Output",
        pluginsystem::PortDirection::output,
        pluginsystem::PortAccessMode::buffered_latest,
        sizeof(Sample),
        alignof(Sample),
        "test.Sample",
    });
    return descriptor;
}

pluginsystem::BuiltinPluginDefinition make_increment_builtin()
{
    pluginsystem::BuiltinPluginDefinition definition;
    definition.descriptor = make_builtin_descriptor(
        "test.builtin.increment",
        "Increment",
        pluginsystem::ConcurrencyPolicy::fully_concurrent
    );
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

pluginsystem::BuiltinPluginDefinition make_serialized_builtin()
{
    pluginsystem::BuiltinPluginDefinition definition;
    definition.descriptor = make_builtin_descriptor(
        "test.builtin.serialized",
        "Check",
        pluginsystem::ConcurrencyPolicy::entrypoint_serialized
    );
    definition.factory = [](const pluginsystem::PluginDescriptor&, const pluginsystem::PluginInstanceConfig&, const pluginsystem::RuntimeBindings&) {
        auto active_calls = std::make_shared<std::atomic<int>>(0);
        return std::make_unique<pluginsystem::BuiltinPluginInstanceBackend>(
            [active_calls](std::string_view entrypoint, pluginsystem::InvocationContext& context) {
                (void)context;
                if (entrypoint != "Check") {
                    return static_cast<int32_t>(PS_NOT_FOUND);
                }

                const int previous = active_calls->fetch_add(1);
                if (previous != 0) {
                    active_calls->fetch_sub(1);
                    return static_cast<int32_t>(PS_ERROR);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds{5});
                active_calls->fetch_sub(1);
                return static_cast<int32_t>(PS_OK);
            }
        );
    };
    return definition;
}

pluginsystem::BuiltinPluginDefinition make_graph_stage_builtin(
    std::string id,
    int32_t add,
    pluginsystem::PortAccessMode input_access = pluginsystem::PortAccessMode::buffered_latest,
    pluginsystem::PortAccessMode output_access = pluginsystem::PortAccessMode::buffered_latest,
    std::string input_type = "test.Sample",
    std::string output_type = "test.Sample",
    std::uint64_t input_size = sizeof(Sample),
    std::uint64_t output_size = sizeof(Sample),
    std::shared_ptr<std::atomic<int>> starts = {},
    std::shared_ptr<std::atomic<int>> stops = {}
)
{
    pluginsystem::PluginDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.name = descriptor.id;
    descriptor.version = "1.0.0";
    descriptor.description = "Built-in graph stage plugin";
    descriptor.concurrency = pluginsystem::ConcurrencyPolicy::instance_serialized;
    descriptor.entrypoints.push_back(pluginsystem::EntrypointDescriptor{
        "Start",
        "Start",
        "Starts graph stage",
        pluginsystem::ConcurrencyPolicy::instance_serialized,
        {},
        {},
    });
    descriptor.entrypoints.push_back(pluginsystem::EntrypointDescriptor{
        "Stop",
        "Stop",
        "Stops graph stage",
        pluginsystem::ConcurrencyPolicy::instance_serialized,
        {},
        {},
    });
    descriptor.entrypoints.push_back(pluginsystem::EntrypointDescriptor{
        "Process",
        "Process",
        "Processes one sample",
        pluginsystem::ConcurrencyPolicy::instance_serialized,
        {"input"},
        {"output"},
    });
    descriptor.ports.push_back(pluginsystem::PortDescriptor{
        "input",
        "Input",
        pluginsystem::PortDirection::input,
        input_access,
        input_size,
        alignof(Sample),
        std::move(input_type),
    });
    descriptor.ports.push_back(pluginsystem::PortDescriptor{
        "output",
        "Output",
        pluginsystem::PortDirection::output,
        output_access,
        output_size,
        alignof(Sample),
        std::move(output_type),
    });

    pluginsystem::BuiltinPluginDefinition definition;
    definition.descriptor = std::move(descriptor);
    definition.factory = [add, starts = std::move(starts), stops = std::move(stops)](
        const pluginsystem::PluginDescriptor&,
        const pluginsystem::PluginInstanceConfig&,
        const pluginsystem::RuntimeBindings&
    ) {
        return std::make_unique<pluginsystem::BuiltinPluginInstanceBackend>(
            [add, starts, stops](std::string_view entrypoint, pluginsystem::InvocationContext& context) {
                if (entrypoint == "Start") {
                    if (starts) {
                        starts->fetch_add(1);
                    }
                    return static_cast<int32_t>(PS_OK);
                }
                if (entrypoint == "Stop") {
                    if (stops) {
                        stops->fetch_add(1);
                    }
                    return static_cast<int32_t>(PS_OK);
                }
                if (entrypoint != "Process") {
                    return static_cast<int32_t>(PS_NOT_FOUND);
                }

                Sample input{};
                context.read_port("input", &input, sizeof(input));
                Sample output{input.value + add};
                context.write_port("output", &output, sizeof(output));
                return static_cast<int32_t>(PS_OK);
            }
        );
    };
    return definition;
}

bool contains_plugin(const std::vector<pluginsystem::PluginDescriptor>& descriptors, std::string_view id)
{
    return std::any_of(descriptors.begin(), descriptors.end(), [id](const auto& descriptor) {
        return descriptor.id == id;
    });
}

void expect_graph_error(const std::function<void()>& action)
{
    bool failed = false;
    try {
        action();
    } catch (const pluginsystem::PluginError&) {
        failed = true;
    }
    assert(failed);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: plugin_manager_tests <test-plugin-library-path> <missing-symbol-plugin-path>\n";
        return 2;
    }

    pluginsystem::PluginRegistry registry;
    registry.add_dll_plugin(argv[1]);
    registry.register_builtin(make_increment_builtin());
    registry.register_builtin(make_serialized_builtin());
    auto graph_starts = std::make_shared<std::atomic<int>>(0);
    auto graph_stops = std::make_shared<std::atomic<int>>(0);
    registry.register_builtin(make_graph_stage_builtin("test.graph.add_one", 1, pluginsystem::PortAccessMode::buffered_latest, pluginsystem::PortAccessMode::buffered_latest, "test.Sample", "test.Sample", sizeof(Sample), sizeof(Sample), graph_starts, graph_stops));
    registry.register_builtin(make_graph_stage_builtin("test.graph.times_ten", 10));
    registry.register_builtin(make_graph_stage_builtin("test.graph.plus_hundred", 100));
    registry.register_builtin(make_graph_stage_builtin("test.graph.type_mismatch", 1, pluginsystem::PortAccessMode::buffered_latest, pluginsystem::PortAccessMode::buffered_latest, "test.Other", "test.Other"));
    registry.register_builtin(make_graph_stage_builtin("test.graph.size_mismatch", 1, pluginsystem::PortAccessMode::buffered_latest, pluginsystem::PortAccessMode::buffered_latest, "test.Sample", "test.Sample", sizeof(Sample) + 4, sizeof(Sample) + 4));
    registry.register_builtin(make_graph_stage_builtin("test.graph.access_mismatch", 1, pluginsystem::PortAccessMode::direct_block, pluginsystem::PortAccessMode::direct_block));

    const auto descriptors = registry.discover_plugins();
    assert(contains_plugin(descriptors, "test.pipeline"));
    assert(contains_plugin(descriptors, "test.builtin.increment"));
    assert(contains_plugin(descriptors, "test.builtin.serialized"));
    assert(contains_plugin(descriptors, "test.graph.add_one"));

    pluginsystem::PluginInstanceConfig config;
    config.blueprint_name = "TestBlueprint";
    config.instance_name = "DllA";
    config.runtime_directory = "pluginsystem_test_runtime";
    auto instance = registry.create_instance("test.pipeline", config);

    pluginsystem::PluginInstanceConfig config_b = config;
    config_b.instance_name = "DllB";
    auto instance_b = registry.create_instance("test.pipeline", config_b);
    assert(!instance->loaded_path().empty());
    assert(!instance_b->loaded_path().empty());
    assert(instance->loaded_path() != instance_b->loaded_path());

    Sample input{12};
    int32_t gain = 5;
    instance->port("input_direct").write(&input, sizeof(input));
    instance->properties().write("gain", &gain, sizeof(gain));

    assert(instance->invoke("Function1") == PS_OK);

    Sample output{};
    instance->port("output_latest").read(&output, sizeof(output));
    assert(output.value == 17);

    int32_t counter = 0;
    instance->properties().read("counter", &counter, sizeof(counter));
    assert(counter == 1);
    assert(*static_cast<int32_t*>(instance->properties().raw_property_block()) == 1);
    assert(instance->port("output_latest").version() > 0);

    auto mapped_reader = pluginsystem::SharedMemoryChannel::open(instance->port("output_latest").name());
    Sample fanout_output{};
    mapped_reader->read(&fanout_output, sizeof(fanout_output));
    assert(fanout_output.value == output.value);

    const auto job = instance->submit("Function2");
    assert(instance->wait(job) == PS_OK);
    assert(instance->result(job).has_value());
    instance->port("output_latest").read(&output, sizeof(output));
    assert(output.value == 106);

    bool size_mismatch_failed = false;
    try {
        instance->port("input_direct").write(&input, 1);
    } catch (const pluginsystem::PluginError&) {
        size_mismatch_failed = true;
    }
    assert(size_mismatch_failed);

    bool duplicate_name_failed = false;
    try {
        auto duplicate = registry.create_instance("test.pipeline", config);
        (void)duplicate;
    } catch (const pluginsystem::PluginError&) {
        duplicate_name_failed = true;
    }
    assert(duplicate_name_failed);

    pluginsystem::PluginInstanceConfig builtin_config;
    builtin_config.blueprint_name = "TestBlueprint";
    builtin_config.instance_name = "BuiltinIncrement";
    auto builtin = registry.create_instance("test.builtin.increment", builtin_config);
    Sample builtin_input{41};
    builtin->port("input").write(&builtin_input, sizeof(builtin_input));
    assert(builtin->invoke("Increment") == PS_OK);
    Sample builtin_output{};
    builtin->port("output").read(&builtin_output, sizeof(builtin_output));
    assert(builtin_output.value == 42);

    pluginsystem::PluginInstanceConfig serialized_config;
    serialized_config.blueprint_name = "TestBlueprint";
    serialized_config.instance_name = "Serialized";
    auto serialized = registry.create_instance("test.builtin.serialized", serialized_config);

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int index = 0; index < 12; ++index) {
        threads.emplace_back([&serialized, &failures]() {
            if (serialized->invoke("Check") != PS_OK) {
                ++failures;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    assert(failures.load() == 0);

    bool invalid_descriptor_failed = false;
    try {
        pluginsystem::PluginRegistry invalid_registry;
        pluginsystem::BuiltinPluginDefinition invalid;
        invalid.descriptor.name = "Invalid";
        invalid.factory = [](const pluginsystem::PluginDescriptor&, const pluginsystem::PluginInstanceConfig&, const pluginsystem::RuntimeBindings&) {
            return std::make_unique<pluginsystem::BuiltinPluginInstanceBackend>(
                [](std::string_view, pluginsystem::InvocationContext&) {
                    return static_cast<int32_t>(PS_OK);
                }
            );
        };
        invalid_registry.register_builtin(std::move(invalid));
    } catch (const pluginsystem::PluginError&) {
        invalid_descriptor_failed = true;
    }
    assert(invalid_descriptor_failed);

    bool missing_symbol_failed = false;
    try {
        pluginsystem::DllPluginProvider missing_symbol_provider{argv[2]};
        (void)missing_symbol_provider.discover();
    } catch (const pluginsystem::PluginError&) {
        missing_symbol_failed = true;
    }
    assert(missing_symbol_failed);

    pluginsystem::GraphConfig graph_config;
    graph_config.blueprint_name = "GraphBlueprintPipeline";
    graph_config.worker_count = 1;
    graph_config.nodes.push_back(pluginsystem::GraphNodeConfig{
        "source",
        "test.graph.add_one",
        "Source",
        "Process",
        "Start",
        "Stop",
    });
    graph_config.nodes.push_back(pluginsystem::GraphNodeConfig{
        "consumer",
        "test.graph.times_ten",
        "Consumer",
        "Process",
        "Start",
        "Stop",
    });
    graph_config.edges.push_back(pluginsystem::GraphEdgeConfig{
        "source",
        "output",
        "consumer",
        "input",
    });
    auto graph = registry.create_graph(graph_config);
    assert(graph->port("source", "output").name() == graph->port("consumer", "input").name());
    Sample graph_input{3};
    graph->port("source", "input").write(&graph_input, sizeof(graph_input));
    const auto graph_job = graph->submit_run();
    const auto graph_status = graph->status(graph_job);
    assert(graph_status == pluginsystem::GraphJobStatus::pending || graph_status == pluginsystem::GraphJobStatus::running || graph_status == pluginsystem::GraphJobStatus::completed);
    const auto graph_result = graph->wait(graph_job);
    assert(graph_result.result == PS_OK);
    assert(graph->result(graph_job).has_value());
    Sample graph_output{};
    graph->port("consumer", "output").read(&graph_output, sizeof(graph_output));
    assert(graph_output.value == 14);
    graph->stop();
    assert(graph_starts->load() == 1);
    assert(graph_stops->load() == 1);

    pluginsystem::GraphConfig fanout_config;
    fanout_config.blueprint_name = "GraphBlueprintFanout";
    fanout_config.nodes.push_back(pluginsystem::GraphNodeConfig{"source", "test.graph.add_one", "FanoutSource"});
    fanout_config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.times_ten", "FanoutA"});
    fanout_config.nodes.push_back(pluginsystem::GraphNodeConfig{"b", "test.graph.plus_hundred", "FanoutB"});
    fanout_config.edges.push_back(pluginsystem::GraphEdgeConfig{"source", "output", "a", "input"});
    fanout_config.edges.push_back(pluginsystem::GraphEdgeConfig{"source", "output", "b", "input"});
    auto fanout = registry.create_graph(fanout_config);
    assert(fanout->port("source", "output").name() == fanout->port("a", "input").name());
    assert(fanout->port("source", "output").name() == fanout->port("b", "input").name());
    Sample fanout_input{2};
    fanout->port("source", "input").write(&fanout_input, sizeof(fanout_input));
    const auto fanout_job = fanout->submit_run();
    assert(fanout->wait(fanout_job).result == PS_OK);
    Sample fanout_a{};
    Sample fanout_b{};
    fanout->port("a", "output").read(&fanout_a, sizeof(fanout_a));
    fanout->port("b", "output").read(&fanout_b, sizeof(fanout_b));
    assert(fanout_a.value == 13);
    assert(fanout_b.value == 103);

    auto make_invalid_graph = [](std::string blueprint_name) {
        pluginsystem::GraphConfig config;
        config.blueprint_name = std::move(blueprint_name);
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.add_one", config.blueprint_name + "A"});
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"b", "test.graph.times_ten", config.blueprint_name + "B"});
        return config;
    };

    expect_graph_error([&registry]() {
        pluginsystem::GraphConfig config;
        config.blueprint_name = "GraphInvalidMissingNode";
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.add_one", "MissingNodeA"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "output", "missing", "input"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry, &make_invalid_graph]() {
        auto config = make_invalid_graph("GraphInvalidMissingPort");
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "missing", "b", "input"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry, &make_invalid_graph]() {
        auto config = make_invalid_graph("GraphInvalidInputToInput");
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "input", "b", "input"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry, &make_invalid_graph]() {
        auto config = make_invalid_graph("GraphInvalidOutputToOutput");
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "output", "b", "output"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry]() {
        pluginsystem::GraphConfig config;
        config.blueprint_name = "GraphInvalidTypeMismatch";
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.add_one", "TypeA"});
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"b", "test.graph.type_mismatch", "TypeB"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "output", "b", "input"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry]() {
        pluginsystem::GraphConfig config;
        config.blueprint_name = "GraphInvalidSizeMismatch";
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.add_one", "SizeA"});
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"b", "test.graph.size_mismatch", "SizeB"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "output", "b", "input"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry]() {
        pluginsystem::GraphConfig config;
        config.blueprint_name = "GraphInvalidAccessMismatch";
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.add_one", "AccessA"});
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"b", "test.graph.access_mismatch", "AccessB"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "output", "b", "input"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry]() {
        pluginsystem::GraphConfig config;
        config.blueprint_name = "GraphInvalidFanin";
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.add_one", "FaninA"});
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"b", "test.graph.times_ten", "FaninB"});
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"c", "test.graph.plus_hundred", "FaninC"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "output", "c", "input"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"b", "output", "c", "input"});
        (void)registry.create_graph(config);
    });
    expect_graph_error([&registry]() {
        pluginsystem::GraphConfig config;
        config.blueprint_name = "GraphInvalidCycle";
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"a", "test.graph.add_one", "CycleA"});
        config.nodes.push_back(pluginsystem::GraphNodeConfig{"b", "test.graph.times_ten", "CycleB"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"a", "output", "b", "input"});
        config.edges.push_back(pluginsystem::GraphEdgeConfig{"b", "output", "a", "input"});
        (void)registry.create_graph(config);
    });

    return 0;
}
