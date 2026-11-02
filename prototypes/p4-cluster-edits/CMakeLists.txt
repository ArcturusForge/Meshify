include(FetchContent)
FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG v1.1.1
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(meshoptimizer)

add_library(p4_clusters STATIC src/cluster_mesh.cpp)
target_include_directories(p4_clusters PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(p4_clusters PUBLIC meshoptimizer Threads::Threads)

add_executable(p4_cluster_tests tests/cluster_tests.cpp)
target_link_libraries(p4_cluster_tests PRIVATE p4_clusters)
add_test(NAME p4_cluster_tests COMMAND p4_cluster_tests)

add_executable(p4_edit_bench src/bench.cpp)
target_link_libraries(p4_edit_bench PRIVATE p4_clusters)
# A small run in CI keeps the pass condition checked on every system; the full
# 5 million vertex run is documented in the README.
add_test(NAME p4_edit_bench_small COMMAND p4_edit_bench --vertices 500000 --edits 40 --budget-ms 15)

# Step 2: a Qt RHI viewer that draws the mesh and times edits through the GPU.
# Needs Qt 6.7 or newer (QRhiWidget) and its shader tools.
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets ShaderTools)
if(Qt6_FOUND AND Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
        find_package(Qt6 QUIET COMPONENTS GuiPrivate)
    endif()
    qt_add_executable(p4_viewer viewer/viewer.cpp)
    qt_add_shaders(p4_viewer "p4_shaders" PREFIX "/shaders" BASE viewer
        FILES viewer/mesh.vert viewer/mesh.frag)
    target_link_libraries(p4_viewer PRIVATE p4_clusters Qt6::Widgets Qt6::GuiPrivate)
    set_target_properties(p4_viewer PROPERTIES OUTPUT_NAME meshify_p4_viewer)
    meshify_install_qt_app(p4_viewer)
else()
    message(STATUS "Qt 6.7 with ShaderTools not found: skipping the P4 viewer")
endif()
