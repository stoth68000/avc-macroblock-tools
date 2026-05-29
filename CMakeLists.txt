cmake_minimum_required(VERSION 3.16)
project(avc_macroblock_tools C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(avcprobe
    src/avc_bitreader.c
    src/avc_nal.c
    src/avc_syntax.c
    src/avc_dpb.c
    src/avc_cabac.c
    src/avc_cavlc.c
    src/avc_macroblock.c
    src/avc_parser.c
)

target_include_directories(avcprobe PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(avcprobe PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(avc-probe tools/avc_probe.c)
target_link_libraries(avc-probe PRIVATE avcprobe)

add_executable(test_bitreader tests/test_bitreader.c)
target_link_libraries(test_bitreader PRIVATE avcprobe)

add_executable(test_entropy tests/test_entropy.c)
target_link_libraries(test_entropy PRIVATE avcprobe)

enable_testing()
add_test(NAME bitreader COMMAND test_bitreader)
add_test(NAME entropy COMMAND test_entropy)
