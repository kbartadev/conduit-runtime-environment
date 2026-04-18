# -------------------------------------------------------------
# AXIOM Examples
# -------------------------------------------------------------

# Collect all .cpp example files
file(GLOB AXIOM_EXAMPLE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Create an executable for each example
foreach(EXAMPLE_FILE ${AXIOM_EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_FILE} NAME_WE)

    add_executable(${EXAMPLE_NAME} ${EXAMPLE_FILE})

    target_link_libraries(${EXAMPLE_NAME}
        PRIVATE
            axiom
    )

    target_compile_features(${EXAMPLE_NAME} PRIVATE cxx_std_20)

    set_property(TARGET ${EXAMPLE_NAME} PROPERTY FOLDER "Examples")

    message(STATUS "AXIOM example added: ${EXAMPLE_NAME}")
endforeach()
