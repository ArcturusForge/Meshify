cmake_minimum_required(VERSION 3.21)
project(Meshify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(MeshifyPackaging)

# Stage 0 prototypes. Each one answers a single architecture question from
# docs/adr/0001-core-stack.md and is not production code.
add_subdirectory(prototypes/p1-token-identity)
add_subdirectory(prototypes/p2-occt-stress)
add_subdirectory(prototypes/p3-docking-shell)
add_subdirectory(prototypes/p4-cluster-edits)

meshify_finalize_install()
