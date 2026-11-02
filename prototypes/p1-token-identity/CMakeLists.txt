find_package(OpenCASCADE REQUIRED CONFIG)

add_library(p1_token_lib STATIC src/naming.cpp src/token.cpp)
target_include_directories(p1_token_lib PUBLIC src)
target_include_directories(p1_token_lib SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(p1_token_lib PUBLIC
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKFillet TKShHealing)

# OCCT 7.8 moved the STL writer from TKSTL into TKDESTL.
if(TARGET TKDESTL)
    set(P1_STL_LIB TKDESTL)
else()
    set(P1_STL_LIB TKSTL)
endif()

add_executable(p1_token src/main.cpp)
target_link_libraries(p1_token PRIVATE p1_token_lib TKMesh ${P1_STL_LIB})

add_executable(p1_token_tests tests/token_tests.cpp)
target_link_libraries(p1_token_tests PRIVATE p1_token_lib)
add_test(NAME p1_token_tests COMMAND p1_token_tests)
