cmake_minimum_required(VERSION 3.20)

project(
    PluginSystem
    VERSION 0.1.0
    DESCRIPTION "Small C++ plugin loading library"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PLUGINSYSTEM_BUILD_EXAMPLES "Build PluginSystem examples" ON)
option(PLUGINSYSTEM_BUILD_TESTS "Build PluginSystem tests" ON)

add_library(pluginsystem STATIC
    src/detail/platform_error.cpp
    src/detail/plugin_utils.cpp
    src/detail/shared_library.cpp
    src/error.cpp
    src/invocation_context.cpp
    src/plugin_instance.cpp
    src/plugin_providers.cpp
    src/plugin_registry.cpp
    src/shared_memory.cpp
    src/shared_property_block.cpp
    src/types.cpp
)

add_library(PluginSystem::pluginsystem ALIAS pluginsystem)

target_compile_features(pluginsystem PUBLIC cxx_std_17)

target_include_directories(pluginsystem
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

if(UNIX)
    target_link_libraries(pluginsystem PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(pluginsystem PRIVATE /W4 /permissive-)
else()
    target_compile_options(pluginsystem PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PLUGINSYSTEM_BUILD_EXAMPLES)
    add_library(greeter_plugin SHARED
        examples/greeter_plugin.cpp
        examples/MyUserPlugin.cpp
    )
    target_link_libraries(greeter_plugin PRIVATE PluginSystem::pluginsystem)
    set_target_properties(greeter_plugin PROPERTIES PREFIX "")

    add_executable(host_app
        examples/host_app.cpp
    )
    target_link_libraries(host_app PRIVATE PluginSystem::pluginsystem)
endif()

if(PLUGINSYSTEM_BUILD_TESTS)
    enable_testing()

    add_library(test_echo_plugin SHARED
        tests/test_echo_plugin.cpp
    )
    target_link_libraries(test_echo_plugin PRIVATE PluginSystem::pluginsystem)
    set_target_properties(test_echo_plugin PROPERTIES PREFIX "")

    add_library(missing_symbol_plugin SHARED
        tests/missing_symbol_plugin.cpp
    )
    target_link_libraries(missing_symbol_plugin PRIVATE PluginSystem::pluginsystem)
    set_target_properties(missing_symbol_plugin PROPERTIES PREFIX "")

    add_executable(plugin_manager_tests
        tests/plugin_manager_tests.cpp
    )
    target_link_libraries(plugin_manager_tests PRIVATE PluginSystem::pluginsystem)

    add_test(
        NAME plugin_manager_tests
        COMMAND plugin_manager_tests "$<TARGET_FILE:test_echo_plugin>" "$<TARGET_FILE:missing_symbol_plugin>"
    )
endif()

install(
    TARGETS pluginsystem
    EXPORT PluginSystemTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/PluginSystemConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/PluginSystemConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PluginSystem
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/PluginSystemConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/PluginSystemConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/PluginSystemConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PluginSystem
)

install(
    EXPORT PluginSystemTargets
    NAMESPACE PluginSystem::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PluginSystem
)
