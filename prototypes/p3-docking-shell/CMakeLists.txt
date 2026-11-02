# P3 needs Qt 6. Where it is missing the prototype is skipped, so the OCCT
# prototypes still build on machines without Qt.
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
if(NOT Qt6_FOUND)
    message(STATUS "Qt 6 not found: skipping P3 docking shell")
    return()
endif()

include(FetchContent)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ads
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG 4.4.1
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(ads)

set(CMAKE_AUTOMOC ON)
add_library(p3_layout STATIC src/layout.cpp src/shell.cpp)
target_include_directories(p3_layout PUBLIC src)
target_link_libraries(p3_layout PUBLIC Qt6::Widgets ads::qtadvanceddocking-qt6)

add_executable(p3_shell src/main.cpp)
target_link_libraries(p3_shell PRIVATE p3_layout)

add_executable(p3_docking_tests tests/docking_tests.cpp)
target_link_libraries(p3_docking_tests PRIVATE p3_layout)
add_test(NAME p3_docking_tests COMMAND p3_docking_tests)
set_tests_properties(p3_docking_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
