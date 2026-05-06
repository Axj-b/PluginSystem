#pragma once

#include "IPlugin.h"

#include <cstdint>
#include <vector>

namespace AutomotiveSensors
{
    inline constexpr uint32_t MAX_POINTS_PER_CLOUD = 100000; ///< Maximum number of points allowed in a point cloud
    inline constexpr uint32_t MAX_OBJECTS_PER_LIST = 1000;   ///< Maximum number of objects allowed in a list
    /// Structure to represent a 3D point in a point cloud
    struct Point3D
    {
        float x;          ///< X coordinate in meters
        float y;          ///< Y coordinate in meters
        float z;          ///< Z coordinate in meters
        float intensity;  ///< Point intensity or reflectance value
        uint8_t reserved; ///< Reserved for future use
    };

    /// Structure to represent a point cloud from LiDAR sensor
    struct PointCloud
    {
        uint64_t timestamp;                   ///< Timestamp in microseconds
        uint32_t sensorId;                    ///< Unique sensor identifier
        uint32_t pointCount;                  ///< Number of points in the cloud
        Point3D points[MAX_POINTS_PER_CLOUD]; ///< Array of 3D points
    };

    /// Structure to represent a detected object
    struct DetectedObject
    {
        uint32_t objectId;   ///< Unique object identifier
        float x;             ///< X position in meters (relative to sensor)
        float y;             ///< Y position in meters
        float z;             ///< Z position in meters
        float vx;            ///< X velocity in m/s
        float vy;            ///< Y velocity in m/s
        float vz;            ///< Z velocity in m/s
        float width;         ///< Object width in meters
        float length;        ///< Object length in meters
        float height;        ///< Object height in meters
        float confidence;    ///< Detection confidence (0.0 to 1.0)
        uint8_t objectClass; ///< Object class (0=unknown, 1=vehicle, 2=pedestrian, 3=cyclist, etc.)
    };

    /// Structure to represent a list of detected objects
    struct ObjectList
    {
        uint64_t timestamp;                           ///< Timestamp in microseconds
        uint32_t sensorId;                            ///< Unique sensor identifier
        uint32_t objectCount;                         ///< Number of objects in the list
        DetectedObject objects[MAX_OBJECTS_PER_LIST]; ///< Array of detected objects
    };

} // namespace AutomotiveSensors

namespace ExamplePluginSdk {

template <>
struct TypeName<AutomotiveSensors::PointCloud> {
    static std::string value()
    {
        return "AutomotiveSensors.PointCloud";
    }
};

template <>
struct TypeName<AutomotiveSensors::ObjectList> {
    static std::string value()
    {
        return "AutomotiveSensors.ObjectList";
    }
};

} // namespace ExamplePluginSdk
