# -------------------------------------------------------------
# CONDUIT Benchmarks (Google Benchmark)
# -------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# Fetch Google Benchmark
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)

# Disable Google Benchmark’s own GTest tests
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googlebenchmark)

# -------------------------------------------------------------
# Collect all benchmark .cpp files
# -------------------------------------------------------------
file(GLOB CONDUIT_BENCH_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# -------------------------------------------------------------
# Create an executable for each benchmark
# -------------------------------------------------------------
foreach(BENCH_FILE ${CONDUIT_BENCH_SOURCES})
    get_filename_component(BENCH_NAME ${BENCH_FILE} NAME_WE)

    add_executable(${BENCH_NAME} ${BENCH_FILE})

    target_link_libraries(${BENCH_NAME}
        PRIVATE
            conduit
            benchmark::benchmark
            benchmark::benchmark_main
    )

    target_compile_features(${BENCH_NAME} PRIVATE cxx_std_20)

    set_property(TARGET ${BENCH_NAME} PROPERTY FOLDER "Benchmarks")

    message(STATUS "CONDUIT benchmark added: ${BENCH_NAME}")
endforeach()
