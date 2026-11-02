# P3 needs Qt 6. Where it is missing the prototype is skipped, so the OCCT
# prototypes still build on machines without Qt.
option(MESHIFY_REQUIRE_QT "Fail instead of skipping P3 when Qt 6 is missing (CI sets this)" OFF)
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
# From Qt 6.9 the private GUI headers are a separate component. ADS links
# Qt6::GuiPrivate on every system but only asks for it itself on Linux.
if(Qt6_FOUND AND Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
    if(NOT Qt6GuiPrivate_FOUND)
        set(Qt6_FOUND FALSE)
    endif()
endif()
if(NOT Qt6_FOUND)
    if(MESHIFY_REQUIRE_QT)
        message(FATAL_ERROR "Qt 6 (with GuiPrivate) is required but was not found")
    endif()
    message(STATUS "Qt 6 not found: skipping P3 docking shell")
    return()
endif()

include(FetchContent)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ads
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG 4.4.1
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL)  # keeps the ADS library out of the install tree
FetchContent_MakeAvailable(ads)

set(CMAKE_AUTOMOC ON)
add_library(p3_layout STATIC src/layout.cpp src/shell.cpp)
target_include_directories(p3_layout PUBLIC src)
target_link_libraries(p3_layout PUBLIC Qt6::Widgets ads::qtadvanceddocking-qt6)

# Shipped as "meshify": the Stage 0 shell, with empty placeholder panels.
add_executable(p3_shell src/main.cpp)
set_target_properties(p3_shell PROPERTIES OUTPUT_NAME meshify)
target_link_libraries(p3_shell PRIVATE p3_layout)
meshify_install(p3_shell)
# Copies Qt's platform plugins (X11, Wayland, ...) and libraries into the release.
# Qt's deploy script generator gained the options used here in Qt 6.5. Older Qt
# (Ubuntu 24.04 has 6.4) builds and runs the shell but cannot make a release archive.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    # The offscreen platform lets the release be smoke-tested without a display.
    # On Linux, also ship native Wayland (Bazzite, Fedora, recent Ubuntu).
    set(p3_extra_plugins qoffscreen)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND p3_extra_plugins qwayland)
    endif()
    qt_generate_deploy_app_script(TARGET p3_shell OUTPUT_SCRIPT p3_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR NO_TRANSLATIONS
        INCLUDE_PLUGINS ${p3_extra_plugins}
        PRE_EXCLUDE_REGEXES ${MESHIFY_PRE_EXCLUDE_REGEXES}
        POST_EXCLUDE_REGEXES ${MESHIFY_POST_EXCLUDE_REGEXES})
    install(SCRIPT ${p3_deploy_script})
endif()

add_executable(p3_docking_tests tests/docking_tests.cpp)
target_link_libraries(p3_docking_tests PRIVATE p3_layout)
add_test(NAME p3_docking_tests COMMAND p3_docking_tests)
set_tests_properties(p3_docking_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
