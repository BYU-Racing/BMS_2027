cmake_minimum_required(VERSION 3.22)

project(BMSUnitTests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(Catch2)

enable_testing()

add_executable(bms_unit_tests
    test_control.cpp
    ../app/Src/control.c
)

target_include_directories(bms_unit_tests PRIVATE
    ../Core/Inc
    ../app/Inc
)

target_link_libraries(bms_unit_tests PRIVATE
    Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(bms_unit_tests)