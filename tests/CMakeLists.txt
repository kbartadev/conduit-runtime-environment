# -------------------------------------------------------------
# AXIOM Tests (GoogleTest)
# -------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

file(GLOB_RECURSE AXIOM_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

foreach(TEST_FILE ${AXIOM_TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    add_executable(${TEST_NAME} ${TEST_FILE})

    target_link_libraries(${TEST_NAME}
        PRIVATE
            axiom
            GTest::gtest
            GTest::gtest_main
    )

    target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    set_property(TARGET ${TEST_NAME} PROPERTY FOLDER "Tests")

    message(STATUS "AXIOM test added: ${TEST_NAME}")
endforeach()
