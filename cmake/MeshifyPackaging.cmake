# Install and packaging rules for release builds.
#
# Targets register with meshify_install(<target>...). Shared libraries they
# load at run time (OCCT and its dependencies) are copied next to them, so a
# release archive runs on a machine without OCCT installed. Operating-system
# libraries are left out.

set(MESHIFY_VERSION "0.0.0" CACHE STRING "Version stamped into release packages")

include(GNUInstallDirs)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

set_property(GLOBAL PROPERTY MESHIFY_INSTALL_TARGETS "")

# Libraries that must come from the user's system, not the archive. Bundling the
# C library or the graphics drivers' front ends breaks on machines newer than
# the build environment. Used for the OCCT dependencies and for Qt's deploy script.
# No backslashes or parentheses: Qt's deploy script generator mangles them.
set(MESHIFY_PRE_EXCLUDE_REGEXES
    "^api-ms-" "^ext-ms-"
    "^libc[.]so" "^libm[.]so" "^libdl[.]so" "^libpthread[.]so" "^librt[.]so"
    "^libresolv[.]so" "^ld-linux" "^libgcc_s[.]so"
    "^libGL[.]so" "^libGLX" "^libGLdispatch" "^libEGL" "^libOpenGL" "^libX11")
set(MESHIFY_POST_EXCLUDE_REGEXES
    "^/lib" "^/usr/lib" "^/System/" "^/usr/lib/system"
    ".*[Ss]ystem32/.*" ".*[Ww]indows/.*")

function(meshify_install)
    set_property(GLOBAL APPEND PROPERTY MESHIFY_INSTALL_TARGETS ${ARGN})
    if(APPLE)
        # macOS resolves @rpath dependencies only through the binary's own
        # rpaths, so keep the build-time library paths for the install step.
        set_target_properties(${ARGN} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    endif()
endfunction()

# For Qt applications: registers the target and bundles Qt's libraries and
# platform plugins (X11, Wayland on Linux, offscreen) next to it. Needs Qt 6.5
# or newer for the deploy script; older Qt still builds the target but the
# release archive would lack Qt.
function(meshify_install_qt_app target)
    meshify_install(${target})
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
        set(extra_plugins qoffscreen)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            list(APPEND extra_plugins qwayland)
        endif()
        qt_generate_deploy_app_script(TARGET ${target} OUTPUT_SCRIPT deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR NO_TRANSLATIONS
            INCLUDE_PLUGINS ${extra_plugins}
            PRE_EXCLUDE_REGEXES ${MESHIFY_PRE_EXCLUDE_REGEXES}
            POST_EXCLUDE_REGEXES ${MESHIFY_POST_EXCLUDE_REGEXES})
        install(SCRIPT ${deploy_script})
    endif()
endfunction()

# Call once, after every add_subdirectory().
function(meshify_finalize_install)
    get_property(targets GLOBAL PROPERTY MESHIFY_INSTALL_TARGETS)
    if(NOT targets)
        return()
    endif()

    install(TARGETS ${targets}
        RUNTIME_DEPENDENCY_SET meshify_runtime_deps
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Windows looks for DLLs beside the executable; Linux and macOS use the rpath above.
    if(WIN32)
        set(dep_dest ${CMAKE_INSTALL_BINDIR})
    else()
        set(dep_dest ${CMAKE_INSTALL_LIBDIR})
    endif()

    set(search_dirs "")
    foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
        list(APPEND search_dirs "${prefix}/bin" "${prefix}/lib")
        if(WIN32)
            # conda puts some DLLs in the environment root, above Library/.
            list(APPEND search_dirs "${prefix}/..")
        endif()
    endforeach()

    install(RUNTIME_DEPENDENCY_SET meshify_runtime_deps
        DIRECTORIES ${search_dirs}
        PRE_EXCLUDE_REGEXES ${MESHIFY_PRE_EXCLUDE_REGEXES}
        POST_EXCLUDE_REGEXES ${MESHIFY_POST_EXCLUDE_REGEXES}
        RUNTIME DESTINATION ${dep_dest}
        LIBRARY DESTINATION ${dep_dest}
        FRAMEWORK DESTINATION ${dep_dest})

    # Ship the MSVC runtime so users do not need the VC++ redistributable.
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
        include(InstallRequiredSystemLibraries)
    endif()

    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/README.md
        DESTINATION .)
endfunction()

set(CPACK_PACKAGE_NAME "Meshify")
set(CPACK_PACKAGE_VENDOR "ArcturusForge")
set(CPACK_PACKAGE_VERSION "${MESHIFY_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
