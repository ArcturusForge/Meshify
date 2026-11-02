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

function(meshify_install)
    set_property(GLOBAL APPEND PROPERTY MESHIFY_INSTALL_TARGETS ${ARGN})
    if(APPLE)
        # macOS resolves @rpath dependencies only through the binary's own
        # rpaths, so keep the build-time library paths for the install step.
        set_target_properties(${ARGN} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
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
        PRE_EXCLUDE_REGEXES
            "^api-ms-" "^ext-ms-"
            "^(lib)?c\\.so" "^libm\\.so" "^libdl\\.so" "^libpthread\\.so" "^librt\\.so"
            "^ld-linux" "^libgcc_s\\.so" "^libGL\\.so" "^libGLX" "^libX11"
        POST_EXCLUDE_REGEXES
            "^/lib" "^/usr/lib" "^/System/" "^/usr/lib/system"
            ".*[Ss]ystem32/.*" ".*[Ww]indows/.*"
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
