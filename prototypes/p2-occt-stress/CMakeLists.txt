find_package(OpenCASCADE REQUIRED CONFIG)

add_executable(p2_occt_stress src/stress.cpp)
target_include_directories(p2_occt_stress SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(p2_occt_stress PRIVATE
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKFillet TKOffset TKShHealing)
add_test(NAME p2_occt_stress COMMAND p2_occt_stress)
