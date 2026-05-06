#include <pluginsystem/plugin_manager.hpp>

#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
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

bool contains_plugin(const std::vector<pluginsystem::PluginDescriptor>& descriptors, std::string_view id)
{
    return std::any_of(descriptors.begin(), descriptors.end(), [id](const auto& descriptor) {
        return descriptor.id == id;
    });
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

    const auto descriptors = registry.discover_plugins();
    assert(contains_plugin(descriptors, "test.pipeline"));
    assert(contains_plugin(descriptors, "test.builtin.increment"));
    assert(contains_plugin(descriptors, "test.builtin.serialized"));

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

    return 0;
}
