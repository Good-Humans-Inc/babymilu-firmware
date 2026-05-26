# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.0.10")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

set(CUSTOM_PHY_INIT_BIN "${CMAKE_SOURCE_DIR}/phy_init/custom_fcc_phy_init_data.bin")
if(EXISTS "${CUSTOM_PHY_INIT_BIN}")
    idf_build_get_property(project_elf EXECUTABLE)
    add_custom_command(TARGET ${project_elf} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CUSTOM_PHY_INIT_BIN}"
                "${CMAKE_BINARY_DIR}/phy_init_data.bin"
        COMMENT "Installing custom FCC PHY init data"
    )
else()
    message(STATUS "Custom FCC PHY init data not found at ${CUSTOM_PHY_INIT_BIN}")
endif()
