#include <pluginsystem/plugin_api.h>

#include <cstring>

namespace {

struct Sample {
    int32_t value;
};

struct TestPlugin {
    int32_t calls{0};
};

const char* function1_inputs[] = {"input_direct"};
const char* function1_outputs[] = {"output_latest"};
const char* function2_outputs[] = {"output_latest"};

const ps_entrypoint_descriptor entrypoints[] = {
    {
        static_cast<uint32_t>(sizeof(ps_entrypoint_descriptor)),
        "Function1",
        "Function 1",
        "Reads input and gain, writes output, updates counter",
        PS_CONCURRENCY_ENTRYPOINT_SERIALIZED,
        function1_inputs,
        1,
        function1_outputs,
        1,
    },
    {
        static_cast<uint32_t>(sizeof(ps_entrypoint_descriptor)),
        "Function2",
        "Function 2",
        "Writes a value from the counter",
        PS_CONCURRENCY_FULLY_CONCURRENT,
        nullptr,
        0,
        function2_outputs,
        1,
    },
};

const ps_port_descriptor ports[] = {
    {
        static_cast<uint32_t>(sizeof(ps_port_descriptor)),
        "input_direct",
        "InputDirect",
        PS_PORT_INPUT,
        PS_PORT_DIRECT_BLOCK,
        sizeof(Sample),
        alignof(Sample),
        "test.Sample",
    },
    {
        static_cast<uint32_t>(sizeof(ps_port_descriptor)),
        "output_latest",
        "OutputLatest",
        PS_PORT_OUTPUT,
        PS_PORT_BUFFERED_LATEST,
        sizeof(Sample),
        alignof(Sample),
        "test.Sample",
    },
};

const ps_property_descriptor properties[] = {
    {
        static_cast<uint32_t>(sizeof(ps_property_descriptor)),
        "gain",
        "Gain",
        "int32",
        sizeof(int32_t),
        1,
        1,
    },
    {
        static_cast<uint32_t>(sizeof(ps_property_descriptor)),
        "counter",
        "Counter",
        "int32",
        sizeof(int32_t),
        1,
        1,
    },
};

const ps_plugin_descriptor descriptor{
    PLUGINSYSTEM_ABI_VERSION,
    static_cast<uint32_t>(sizeof(ps_plugin_descriptor)),
    "test.pipeline",
    "Pipeline Test Plugin",
    "2.0.0",
    "Plugin used by PluginSystem runtime tests",
    PS_CONCURRENCY_ENTRYPOINT_SERIALIZED,
    entrypoints,
    2,
    ports,
    2,
    properties,
    2,
    sizeof(int32_t),
};

int32_t invoke(void* instance, const char* entrypoint_id, const ps_invocation_context* context)
{
    auto* plugin = static_cast<TestPlugin*>(instance);
    if (plugin == nullptr || entrypoint_id == nullptr || context == nullptr) {
        return PS_INVALID_ARGUMENT;
    }

    int32_t gain = 0;
    if (context->read_property(context->user_data, "gain", &gain, sizeof(gain)) != PS_OK) {
        return PS_ERROR;
    }

    int32_t counter = 0;
    (void)context->read_property(context->user_data, "counter", &counter, sizeof(counter));

    Sample output{};
    if (std::strcmp(entrypoint_id, "Function1") == 0) {
        Sample input{};
        if (context->read_port(context->user_data, "input_direct", &input, sizeof(input)) != PS_OK) {
            return PS_ERROR;
        }
        output.value = input.value + gain;
    } else if (std::strcmp(entrypoint_id, "Function2") == 0) {
        output.value = counter + gain + 100;
    } else {
        return PS_NOT_FOUND;
    }

    if (context->write_port(context->user_data, "output_latest", &output, sizeof(output)) != PS_OK) {
        return PS_ERROR;
    }

    ++plugin->calls;
    ++counter;
    if (context->write_property(context->user_data, "counter", &counter, sizeof(counter)) != PS_OK) {
        return PS_ERROR;
    }

    uint64_t raw_size = 0;
    auto* raw_counter = static_cast<int32_t*>(context->get_raw_property_block(context->user_data, &raw_size));
    if (raw_counter != nullptr && raw_size >= sizeof(int32_t)) {
        *raw_counter = plugin->calls;
    }

    return PS_OK;
}

void destroy(void* instance)
{
    delete static_cast<TestPlugin*>(instance);
}

} // namespace

extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_discover_plugin(
    const ps_host_context* host,
    ps_plugin_discovery* out_discovery
)
{
    (void)host;
    if (out_discovery == nullptr || out_discovery->abi_version != PLUGINSYSTEM_ABI_VERSION) {
        return PS_INVALID_ARGUMENT;
    }

    out_discovery->descriptor = &descriptor;
    return PS_OK;
}

extern "C" PLUGINSYSTEM_EXPORT int32_t pluginsystem_create_plugin_instance(
    const ps_host_context* host,
    const ps_instance_config* config,
    ps_plugin_instance* out_instance
)
{
    (void)host;
    if (config == nullptr || out_instance == nullptr || config->abi_version != PLUGINSYSTEM_ABI_VERSION) {
        return PS_INVALID_ARGUMENT;
    }
    if (config->port_count != 2 || config->property_count != 2 || config->raw_property_block_size < sizeof(int32_t)) {
        return PS_ERROR;
    }

    out_instance->abi_version = PLUGINSYSTEM_ABI_VERSION;
    out_instance->struct_size = static_cast<uint32_t>(sizeof(ps_plugin_instance));
    out_instance->instance = new TestPlugin();
    out_instance->invoke = invoke;
    out_instance->destroy = destroy;
    return PS_OK;
}
