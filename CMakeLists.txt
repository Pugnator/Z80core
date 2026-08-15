cmake_minimum_required(VERSION 3.13)
project(z80core LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# Parser and lexer are generated into the build tree; they are never committed.
bison_target(z80_parser src/grammar/z80.y ${CMAKE_CURRENT_BINARY_DIR}/grammar.c
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/grammar.h
             COMPILE_FLAGS "-t -v")
flex_target(z80_lexer src/grammar/z80.lex ${CMAKE_CURRENT_BINARY_DIR}/z80lex.c
            COMPILE_FLAGS "-f -i")
add_flex_bison_dependency(z80_lexer z80_parser)

add_executable(zasm
    src/asmdasm/main.c
    src/asmdasm/assembler.c
    src/asmdasm/dassembler.c
    src/asmdasm/compat.c
    src/asmdasm/z80tab.c
    src/asmdasm/tap.c
    src/asmdasm/ihex.c
    src/asmdasm/utils.c
    ${BISON_z80_parser_OUTPUTS}
    ${FLEX_z80_lexer_OUTPUTS})

target_include_directories(zasm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/asmdasm
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR})

if(WIN32)
    target_compile_definitions(zasm PRIVATE
        _WIN32_WINNT=0x0600
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE)
    enable_language(RC)
    target_sources(zasm PRIVATE src/app.rc)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(zasm PRIVATE -Wall -ffunction-sections -fdata-sections)
    target_link_options(zasm PRIVATE -Wl,--gc-sections)
endif()

# The emulator core is not built yet; enable once src/emucore grows up.
# add_executable(zemu src/emucore/cpu.cc src/emucore/exec.cc ...)

enable_testing()

add_test(NAME selftest_monitor48k
         COMMAND zasm -s ${CMAKE_CURRENT_SOURCE_DIR}/tests/monitor48k/monitor48k.asm
                 -o monitor48k.bin -x bin)
add_test(NAME selftest_fallingblocks
         COMMAND zasm -s ${CMAKE_CURRENT_SOURCE_DIR}/tests/fallingblocks/fallingblocks.asm
                 -o fallingblocks.tap -x tap)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME opcode_suite
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py
                     --zasm $<TARGET_FILE:zasm>
                     --tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/asm_test.asm)
endif()
