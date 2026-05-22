cmake_minimum_required(VERSION 3.16)

project(slb_control_polar_encoder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(slb_control_polar_encoder
    src/control_polar_encoder.cpp
    src/polar_reliability_sequence.cpp
)

target_include_directories(slb_control_polar_encoder
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(slb_control_polar_encoder
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
)

enable_testing()

add_executable(control_polar_encoder_tests
    tests/test_control_polar_encoder.cpp
)

target_link_libraries(control_polar_encoder_tests
    PRIVATE
        slb_control_polar_encoder
)

target_compile_options(control_polar_encoder_tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
)

add_test(NAME control_polar_encoder_tests COMMAND control_polar_encoder_tests)
