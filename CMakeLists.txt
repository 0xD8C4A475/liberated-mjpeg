cmake_minimum_required(VERSION 3.10)
project(claude-mjpeg C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

# Library
add_library(claude_mjpeg
    src/claude_mjpeg.c
    src/jpeg_parser.c
    src/huffman.c
    src/idct.c
    src/jpeg_decoder.c
    src/avi_demuxer.c
    src/idct_fast.c
    src/huffman_fast.c
    src/simd_detect.c
    src/idct_simd.c
)
target_include_directories(claude_mjpeg PUBLIC include)
target_include_directories(claude_mjpeg PRIVATE src)
if(NOT MSVC)
    target_link_libraries(claude_mjpeg PRIVATE m)
endif()

# CLI decode tool
add_executable(cmj_decode tools/cmj_decode.c)
target_link_libraries(cmj_decode PRIVATE claude_mjpeg)

# Benchmark tool
add_executable(cmj_bench tools/cmj_bench.c)
target_link_libraries(cmj_bench PRIVATE claude_mjpeg)

# Tests
add_executable(test_parser tests/test_parser.c)
target_link_libraries(test_parser PRIVATE claude_mjpeg)
target_include_directories(test_parser PRIVATE src)
enable_testing()
add_test(NAME parser_test COMMAND test_parser)

add_executable(test_huffman tests/test_huffman.c)
target_link_libraries(test_huffman PRIVATE claude_mjpeg)
target_include_directories(test_huffman PRIVATE src)
add_test(NAME huffman_test COMMAND test_huffman)

add_executable(gen_test_jpeg tests/gen_test_jpeg.c)
if(NOT MSVC)
    target_link_libraries(gen_test_jpeg PRIVATE m)
endif()

add_executable(test_decoder tests/test_decoder.c)
target_link_libraries(test_decoder PRIVATE claude_mjpeg)
target_include_directories(test_decoder PRIVATE src)
add_test(NAME decoder_test COMMAND test_decoder)

# Platform-specific for mkdir in cmj_decode
if(WIN32)
    target_compile_definitions(cmj_decode PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(cmj_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
