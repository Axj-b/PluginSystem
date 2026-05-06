#include "MyUserPlugin.h"

#include <algorithm>
#include <stdexcept>

void MyUserPlugin::Register(ExamplePluginSdk::PluginRegistration<MyUserPlugin>& api)
{
    api.set_plugin(
        "example.greeter",
        "User Object Tracker Plugin",
        "1.0.0",
        "Example user-authored plugin wrapped by the PluginSystem C ABI adapter",
        PS_CONCURRENCY_ENTRYPOINT_SERIALIZED
    );

    api.input(&MyUserPlugin::point_cloud_input_, ExamplePluginSdk::PortAccessMode::DirectBlock);
    api.output(&MyUserPlugin::object_list_output_, ExamplePluginSdk::PortAccessMode::BufferedLatest);
    api.property(&MyUserPlugin::min_confidence_, true, true);

    api.entrypoint("Process", &MyUserPlugin::Process)
        .description("Runs one object-tracking step from PointCloud to ObjectListOutput")
        .concurrency(PS_CONCURRENCY_ENTRYPOINT_SERIALIZED)
        .reads(&MyUserPlugin::point_cloud_input_)
        .writes(&MyUserPlugin::object_list_output_)
        .triggeredBy(&MyUserPlugin::point_cloud_input_);

    api.entrypoint("Reset", &MyUserPlugin::Reset)
        .description("Clears the current output object list")
        .concurrency(PS_CONCURRENCY_ENTRYPOINT_SERIALIZED)
        .writes(&MyUserPlugin::object_list_output_);
}

int MyUserPlugin::Init()
{
    point_cloud_ = std::make_unique<AutomotiveSensors::PointCloud>();
    object_list_ = std::make_unique<AutomotiveSensors::ObjectList>();
    return PS_OK;
}

int MyUserPlugin::Start()
{
    started_ = true;
    return PS_OK;
}

int MyUserPlugin::Stop()
{
    started_ = false;
    return PS_OK;
}

void* MyUserPlugin::GetRenderer()
{
    return nullptr;
}

void MyUserPlugin::Process()
{
    if (!point_cloud_ || !object_list_) {
        throw std::runtime_error{"Plugin was not initialized."};
    }

    const auto min_confidence = min_confidence_.read();
    point_cloud_input_.read(*point_cloud_);

    *object_list_ = {};
    object_list_->timestamp = point_cloud_->timestamp;
    object_list_->sensorId = point_cloud_->sensorId;

    if (started_ && point_cloud_->pointCount > 0 && point_cloud_->points[0].intensity >= min_confidence) {
        const auto object_count = std::min<std::uint32_t>(point_cloud_->pointCount, 1);
        object_list_->objectCount = object_count;

        const auto& first_point = point_cloud_->points[0];
        auto& object = object_list_->objects[0];
        object.objectId = 1;
        object.x = first_point.x;
        object.y = first_point.y;
        object.z = first_point.z;
        object.vx = 0.0F;
        object.vy = 0.0F;
        object.vz = 0.0F;
        object.width = 1.8F;
        object.length = 4.5F;
        object.height = 1.6F;
        object.confidence = first_point.intensity;
        object.objectClass = 1;
    }

    object_list_output_.write(*object_list_);
}

void MyUserPlugin::Reset()
{
    if (!object_list_) {
        throw std::runtime_error{"Plugin was not initialized."};
    }

    *object_list_ = {};
    object_list_output_.write(*object_list_);
}
