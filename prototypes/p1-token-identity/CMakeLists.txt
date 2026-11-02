find_package(OpenCASCADE REQUIRED CONFIG)

add_library(p1_token_lib STATIC src/naming.cpp src/token.cpp)
target_include_directories(p1_token_lib PUBLIC src)
target_include_directories(p1_token_lib SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(p1_token_lib PUBLIC
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKFillet TKShHealing)

add_executable(p1_token src/main.cpp)
target_link_libraries(p1_token PRIVATE p1_token_lib TKMesh TKSTL)

add_executable(p1_token_tests tests/token_tests.cpp)
target_link_libraries(p1_token_tests PRIVATE p1_token_lib)
add_test(NAME p1_token_tests COMMAND p1_token_tests)
