# P5 needs Qt Core only (QProcess and JSON) plus a Python 3 interpreter at run
# time. Where Qt is missing the prototype is skipped, like P3.
option(MESHIFY_REQUIRE_QT "Fail instead of skipping P5 when Qt 6 is missing (CI sets this)" OFF)
find_package(Qt6 QUIET COMPONENTS Core)
if(NOT Qt6_FOUND)
    if(MESHIFY_REQUIRE_QT)
        message(FATAL_ERROR "Qt 6 is required but was not found")
    endif()
    message(STATUS "Qt 6 not found: skipping P5 plugin history")
    return()
endif()

set(CMAKE_AUTOMOC OFF)
add_library(p5_history STATIC src/geometry.cpp src/geometry_io.cpp src/plugin_host.cpp src/document.cpp)
target_include_directories(p5_history PUBLIC src)
target_link_libraries(p5_history PUBLIC Qt6::Core)

set(P5_DEFS
    P5_HOST_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/plugins/meshify_host.py"
    P5_PLUGINS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/plugins"
    P5_TEST_PLUGINS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins")

add_executable(p5_history_tests tests/plugin_history_tests.cpp)
target_link_libraries(p5_history_tests PRIVATE p5_history)
target_compile_definitions(p5_history_tests PRIVATE ${P5_DEFS})
add_test(NAME p5_history_tests COMMAND p5_history_tests)

add_executable(p5_plugin_bench src/bench.cpp)
target_link_libraries(p5_plugin_bench PRIVATE p5_history)
target_compile_definitions(p5_plugin_bench PRIVATE ${P5_DEFS})
