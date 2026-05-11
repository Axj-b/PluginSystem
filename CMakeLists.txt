cmake_minimum_required(VERSION 3.20)

project(
    PluginSystem
    VERSION 0.1.0
    DESCRIPTION "Small C++ plugin loading library"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(PROJECT_IS_TOP_LEVEL)
    set(PLUGINSYSTEM_DEFAULT_BUILD_EXAMPLES ON)
    set(PLUGINSYSTEM_DEFAULT_BUILD_TESTS ON)
else()
    set(PLUGINSYSTEM_DEFAULT_BUILD_EXAMPLES OFF)
    set(PLUGINSYSTEM_DEFAULT_BUILD_TESTS OFF)
endif()

option(PLUGINSYSTEM_BUILD_EXAMPLES "Build PluginSystem examples" ${PLUGINSYSTEM_DEFAULT_BUILD_EXAMPLES})
option(PLUGINSYSTEM_BUILD_TESTS "Build PluginSystem tests" ${PLUGINSYSTEM_DEFAULT_BUILD_TESTS})

add_library(pluginsystem STATIC
    src/detail/platform_error.cpp
    src/detail/plugin_utils.cpp
    src/detail/shared_library.cpp
    src/error.cpp
    src/graph_runtime.cpp
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

add_library(pluginsystem_plugin_sdk INTERFACE)
target_include_directories(pluginsystem_plugin_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/examples>
)
target_link_libraries(pluginsystem_plugin_sdk INTERFACE PluginSystem::pluginsystem)

add_library(pluginsystem_host_sdk STATIC
    examples/host_sdk/dll_plugin_provider.cpp
    examples/host_sdk/recorder_plugin.cpp
    examples/host_sdk/recorder_plugins.cpp
    examples/host_sdk/recording_format.cpp
    examples/host_sdk/replay_plugin.cpp
)
target_include_directories(pluginsystem_host_sdk
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/examples/host_sdk>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(pluginsystem_host_sdk PUBLIC PluginSystem::pluginsystem)

if(PLUGINSYSTEM_BUILD_EXAMPLES)
    include(FetchContent)
    find_package(OpenGL REQUIRED)

    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
    )
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.7
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw nlohmann_json imgui)

    set(PLUGINSYSTEM_IMNODES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/examples/node_editor/3rd-party/imnodes")
    if(NOT EXISTS "${PLUGINSYSTEM_IMNODES_SOURCE_DIR}/imnodes.cpp")
        message(FATAL_ERROR "Vendored imnodes source not found at ${PLUGINSYSTEM_IMNODES_SOURCE_DIR}")
    endif()

    add_library(pluginsystem_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(pluginsystem_imgui
        PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(pluginsystem_imgui PUBLIC glfw OpenGL::GL)

    add_library(pluginsystem_imnodes STATIC
        ${PLUGINSYSTEM_IMNODES_SOURCE_DIR}/imnodes.cpp
    )
    target_include_directories(pluginsystem_imnodes PUBLIC ${PLUGINSYSTEM_IMNODES_SOURCE_DIR})
    target_link_libraries(pluginsystem_imnodes PUBLIC pluginsystem_imgui)

    add_library(greeter_plugin SHARED
        examples/greeter_plugin.cpp
        examples/MyUserPlugin.cpp
    )
    target_link_libraries(greeter_plugin PRIVATE pluginsystem_plugin_sdk)
    set_target_properties(greeter_plugin PROPERTIES PREFIX "")

    add_library(pipeline_producer_plugin SHARED
        examples/pipeline_producer_plugin.cpp
        examples/PipelineProducerPlugin.cpp
    )
    target_link_libraries(pipeline_producer_plugin PRIVATE pluginsystem_plugin_sdk)
    set_target_properties(pipeline_producer_plugin PROPERTIES PREFIX "")

    add_library(pipeline_processor_plugin SHARED
        examples/pipeline_processor_plugin.cpp
        examples/PipelineProcessorPlugin.cpp
    )
    target_link_libraries(pipeline_processor_plugin PRIVATE pluginsystem_plugin_sdk pluginsystem_imgui)
    set_target_properties(pipeline_processor_plugin PROPERTIES PREFIX "")

    add_library(pipeline_presenter_plugin SHARED
        examples/pipeline_presenter_plugin.cpp
        examples/PipelinePresenterPlugin.cpp
    )
    target_link_libraries(pipeline_presenter_plugin PRIVATE pluginsystem_plugin_sdk)
    set_target_properties(pipeline_presenter_plugin PROPERTIES PREFIX "")

    add_executable(host_app
        examples/host_app.cpp
    )
    target_link_libraries(host_app PRIVATE pluginsystem_host_sdk)
    target_include_directories(host_app PRIVATE examples)

    add_executable(graph_pipeline_app
        examples/graph_pipeline_app.cpp
    )
    target_link_libraries(graph_pipeline_app PRIVATE pluginsystem_host_sdk)
    target_include_directories(graph_pipeline_app PRIVATE examples)

    add_library(pluginsystem_node_editor STATIC
        examples/node_editor/editor_model.cpp
        examples/node_editor/node_editor_widget.cpp
    )
    target_include_directories(pluginsystem_node_editor PUBLIC examples/node_editor examples)
    target_link_libraries(pluginsystem_node_editor
        PUBLIC
            pluginsystem_host_sdk
            pluginsystem_imgui
            pluginsystem_imnodes
            nlohmann_json::nlohmann_json
    )

    add_executable(node_editor_app
        examples/node_editor/node_editor_app.cpp
    )
    target_link_libraries(node_editor_app PRIVATE pluginsystem_node_editor)

    if(PLUGINSYSTEM_BUILD_TESTS)
        add_executable(node_editor_widget_smoke
            tests/node_editor_widget_smoke.cpp
        )
        target_link_libraries(node_editor_widget_smoke PRIVATE pluginsystem_node_editor)
    endif()
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

    add_library(sdk_no_base_plugin SHARED
        tests/sdk_no_base_plugin.cpp
    )
    target_link_libraries(sdk_no_base_plugin PRIVATE pluginsystem_plugin_sdk)
    set_target_properties(sdk_no_base_plugin PROPERTIES PREFIX "")

    add_executable(plugin_manager_tests
        tests/plugin_manager_tests.cpp
    )
    target_link_libraries(plugin_manager_tests PRIVATE pluginsystem_host_sdk)

    add_test(
        NAME plugin_manager_tests
        COMMAND plugin_manager_tests
            "$<TARGET_FILE:test_echo_plugin>"
            "$<TARGET_FILE:missing_symbol_plugin>"
            "$<TARGET_FILE:sdk_no_base_plugin>"
    )
endif()

if(PLUGINSYSTEM_BUILD_EXAMPLES AND PLUGINSYSTEM_BUILD_TESTS)
    add_test(
        NAME node_editor_widget_smoke
        COMMAND node_editor_widget_smoke
    )
    add_test(
        NAME graph_pipeline_app
        COMMAND graph_pipeline_app
            "$<TARGET_FILE:pipeline_producer_plugin>"
            "$<TARGET_FILE:pipeline_processor_plugin>"
            "$<TARGET_FILE:pipeline_presenter_plugin>"
            5
    )
    add_test(
        NAME node_editor_headless_pipeline
        COMMAND node_editor_app
            --headless-run
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/node_editor/pipeline_graph.json"
            "$<TARGET_FILE:pipeline_producer_plugin>"
            "$<TARGET_FILE:pipeline_processor_plugin>"
            "$<TARGET_FILE:pipeline_presenter_plugin>"
    )
    add_test(
        NAME node_editor_headless_recorder
        COMMAND node_editor_app
            --headless-run
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/node_editor/recorder_graph.json"
            "$<TARGET_FILE:pipeline_producer_plugin>"
    )
    set_tests_properties(node_editor_headless_recorder PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(
        NAME node_editor_headless_replay
        COMMAND node_editor_app
            --headless-run
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/node_editor/replay_graph.json"
            "$<TARGET_FILE:pipeline_presenter_plugin>"
    )
    set_tests_properties(node_editor_headless_replay PROPERTIES
        DEPENDS node_editor_headless_recorder
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
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
