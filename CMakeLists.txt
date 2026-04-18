cmake_minimum_required(VERSION 3.20)

project(axiom
    VERSION 1.0.0
    DESCRIPTION "Deterministic Distributed Event Runtime"
    LANGUAGES CXX
)

# -------------------------------------------------------------
# Global configuration
# -------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------------------------------------------
# Header-only AXIOM library
# -------------------------------------------------------------
add_library(axiom INTERFACE)

target_include_directories(axiom INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# -------------------------------------------------------------
# Examples
# -------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# -------------------------------------------------------------
# Tests
# -------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

# -------------------------------------------------------------
# Benchmarks
# -------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    enable_testing()
    add_subdirectory(benchmarks)
endif()

# -------------------------------------------------------------
# Installation (optional but professional)
# -------------------------------------------------------------
install(TARGETS axiom
    EXPORT axiomTargets
    INCLUDES DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT axiomTargets
    FILE axiomTargets.cmake
    NAMESPACE axiom::
    DESTINATION lib/cmake/axiom
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/axiomConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/axiomConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/axiomConfig.cmake"
    INSTALL_DESTINATION lib/cmake/axiom
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/axiomConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/axiomConfigVersion.cmake"
    DESTINATION lib/cmake/axiom
)
