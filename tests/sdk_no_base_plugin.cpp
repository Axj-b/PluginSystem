#include <pluginsystem/sdk.hpp>

#include <sdk/dll_adapter.hpp>

#include <cstdint>

namespace {

struct Sample {
    std::int32_t value;
};

} // namespace

namespace pluginsystem::sdk {

template <>
struct TypeName<Sample> {
    static std::string value()
    {
        return "test.Sample";
    }
};

} // namespace pluginsystem::sdk

namespace {

class SdkNoBasePlugin {
public:
    static void Register(pluginsystem::sdk::PluginRegistration<SdkNoBasePlugin>& api)
    {
        api.set_plugin(
            "test.sdk.no_base",
            "SDK No-Base Test Plugin",
            "1.0.0",
            "Verifies PluginRegistration and DllAdapter do not require inheritance",
            PS_CONCURRENCY_INSTANCE_SERIALIZED
        );

        api.input(&SdkNoBasePlugin::input_, pluginsystem::sdk::PortAccessMode::DirectBlock);
        api.output(&SdkNoBasePlugin::output_, pluginsystem::sdk::PortAccessMode::BufferedLatest);
        api.entrypoint("Increment", &SdkNoBasePlugin::Increment)
            .description("Adds one to the input sample")
            .reads(&SdkNoBasePlugin::input_)
            .writes(&SdkNoBasePlugin::output_);
    }

    int Init()
    {
        initialized_ = true;
        return PS_OK;
    }

    int Stop()
    {
        stopped_ = true;
        return PS_OK;
    }

    void Increment()
    {
        Sample input{};
        input_.read(input);
        const Sample output{initialized_ && !stopped_ ? input.value + 1 : -1};
        output_.write(output);
    }

private:
    pluginsystem::sdk::InputPort<Sample> input_{"input"};
    pluginsystem::sdk::OutputPort<Sample> output_{"output"};
    bool initialized_{false};
    bool stopped_{false};
};

} // namespace

PLUGINSYSTEM_EXPORT_PLUGIN(SdkNoBasePlugin)
