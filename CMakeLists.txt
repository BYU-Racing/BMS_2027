cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME BMSRework)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    app/bench/Src/bench_app.c
    app/bench/Src/bench_gpio.c
    app/bench/Src/board_check.c
    app/bench/Src/cell_test.c
    app/bench/Src/fast_poll.c
    app/bench/Src/fault.c
    app/bench/Src/isospi.c
    app/bench/Src/isospi_test.c
    app/bench/Src/led.c
    app/bench/Src/swo.c
    app/bench/Src/thermistor.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    app/bench/Inc
)

set(BMS_TEST_TASK "3" CACHE STRING "Bench test: 0 detailed, 1 fast poll, 2 link, 3 board check")
set_property(CACHE BMS_TEST_TASK PROPERTY STRINGS 0 1 2 3)
if(NOT BMS_TEST_TASK MATCHES "^[0-3]$")
    message(FATAL_ERROR "BMS_TEST_TASK must be 0, 1, 2, or 3")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    BMS_TEST_TASK=${BMS_TEST_TASK}
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Images for STM32CubeProgrammer over ST-Link/SWD or the ROM USB DFU bootloader.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    BYPRODUCTS
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    VERBATIM
)
