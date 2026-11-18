cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Keep this before project(): your toolchain uses the project name.
set(CMAKE_PROJECT_NAME BMS_ABA)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(${CMAKE_PROJECT_NAME} LANGUAGES C CXX ASM)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

add_executable(${CMAKE_PROJECT_NAME})

add_subdirectory(cmake/stm32cubemx)

# Function to allow adding a whole source directory
function(add_source_directory target directory)
    set(module_dir "${CMAKE_CURRENT_SOURCE_DIR}/${directory}")

    file(GLOB_RECURSE module_files CONFIGURE_DEPENDS
        "${module_dir}/*.c"
        "${module_dir}/*.cpp"
        "${module_dir}/*.h"
        "${module_dir}/*.hpp"
    )

    target_sources(${target} PRIVATE ${module_files})

    target_include_directories(${target} PRIVATE
        "${module_dir}"
    )
endfunction()

# Set firmware selection variable
set(FIRMWARE "blinky" CACHE STRING "Firmware image to build")

set_property(CACHE FIRMWARE PROPERTY STRINGS
    blinky
    bms
    peripheral_test
)

if(NOT FIRMWARE MATCHES "^(blinky|bms|peripheral_test)$")
    message(FATAL_ERROR "Unknown FIRMWARE: ${FIRMWARE}")
endif()

message(STATUS "Selected firmware: ${FIRMWARE}")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware"
)

# Collect files only from the selected firmware directory.
add_source_directory(${CMAKE_PROJECT_NAME}
    "firmware/${FIRMWARE}"
)

# Select the supporting application/device modules.
if(FIRMWARE STREQUAL "bms")
    add_source_directory(${CMAKE_PROJECT_NAME} app/config)
    add_source_directory(${CMAKE_PROJECT_NAME} app/control)
    add_source_directory(${CMAKE_PROJECT_NAME} app/tasks)

    add_source_directory(${CMAKE_PROJECT_NAME} devices/ltc6811)
    add_source_directory(${CMAKE_PROJECT_NAME} devices/flash_chip)

elseif(FIRMWARE STREQUAL "peripheral_test")
    add_source_directory(${CMAKE_PROJECT_NAME} devices/flash_chip)

    # Add your target test-runner and QuadSPI test sources here
    # when those files exist.
endif()

# Shared board support.
add_source_directory(${CMAKE_PROJECT_NAME} board)