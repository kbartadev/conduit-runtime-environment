# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

include(FetchContent)
include(GoogleTest)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# 1. Collect Core Sources
file(GLOB CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 2. CREATE INDIVIDUAL TARGETS (For "Select Startup Item")
foreach(TEST_FILE ${CORE_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_FILE} "test_mixed_dispatch_matrix.cpp")
    target_link_libraries(${TEST_NAME} PRIVATE conduit gtest gtest_main)
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)
    gtest_discover_tests(${TEST_NAME} DISCOVERY_TIMEOUT 20)
    set_property(TARGET ${TEST_NAME} PROPERTY FOLDER "Tests/Core/Individual")
endforeach()

# 3. CREATE UNIFIED TARGET (For "Run All Core")
set(CORE_UNIFIED conduit_core_all)
add_executable(${CORE_UNIFIED} ${CORE_SOURCES} "test_mixed_dispatch_matrix.cpp")
target_link_libraries(${CORE_UNIFIED} PRIVATE conduit gtest gtest_main)
target_compile_features(${CORE_UNIFIED} PRIVATE cxx_std_20)
gtest_discover_tests(${CORE_UNIFIED} DISCOVERY_TIMEOUT 60)
set_property(TARGET ${CORE_UNIFIED} PROPERTY FOLDER "Tests/Core")

# 4. Descend into Net
add_subdirectory(net)