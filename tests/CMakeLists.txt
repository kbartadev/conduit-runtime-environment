# -------------------------------------------------------------
# CONDUIT Tests (Unified GoogleTest Runner)
# -------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

include(FetchContent)
include(GoogleTest) # Required for gtest_discover_tests

# 1. GoogleTest Dependency Management (Your original logic)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# 2. Collect all test source files recursively
file(GLOB_RECURSE CONDUIT_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 3. Create a single test runner executable
# This replaces the foreach loop to compile all tests into one binary
set(TEST_RUNNER_NAME conduit_test_runner)
add_executable(${TEST_RUNNER_NAME} ${CONDUIT_TEST_SOURCES})

# 4. Link dependencies
# Links to your library (conduit) and the FetchContent GTest targets
target_link_libraries(${TEST_RUNNER_NAME}
    PRIVATE
        conduit
        gtest
        gtest_main
)

# 5. Set C++ standard
target_compile_features(${TEST_RUNNER_NAME} PRIVATE cxx_std_20)

# 6. Discover tests automatically
# This allows CTest/IDE to see individual TEST() macros inside the single binary
gtest_discover_tests(${TEST_RUNNER_NAME})

# 7. Organization
set_property(TARGET ${TEST_RUNNER_NAME} PROPERTY FOLDER "Tests")

message(STATUS "[CONDUIT] Created unified test runner: ${TEST_RUNNER_NAME}")