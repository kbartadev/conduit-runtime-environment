cmake_minimum_required(VERSION 3.20)

project(conduit
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
# Header-only CONDUIT library
# -------------------------------------------------------------
add_library(conduit INTERFACE)

target_include_directories(conduit INTERFACE
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
# Installation
# -------------------------------------------------------------
install(TARGETS conduit
    EXPORT conduitTargets
    INCLUDES DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT conduitTargets
    FILE conduitTargets.cmake
    NAMESPACE cre::
    DESTINATION lib/cmake/conduit
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/conduitConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/conduitConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/conduitConfig.cmake"
    INSTALL_DESTINATION lib/cmake/conduit
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/conduitConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/conduitConfigVersion.cmake"
    DESTINATION lib/cmake/conduit
)
