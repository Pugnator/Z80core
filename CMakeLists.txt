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

# The disassembler, as a library anything can link: the opcode table and the
# C library are all it needs - no globals, no output, no generated headers.
# BUILD_SHARED_LIBS=ON makes this and zasmlib shared instead of static.
# MinGW exports everything from a DLL by default and MSVC exports nothing, so
# say it explicitly and get the same library either way.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(zdasm src/asmdasm/zdasm.c src/asmdasm/z80tab.c)
target_include_directories(zdasm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/asmdasm)
set_target_properties(zdasm PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The assembler, as a library. Unlike zdasm it needs the generated parser and
# scanner, and it keeps its state in globals, so only one assembly may be in
# flight at a time - see zasm.h and docs/EMBEDDING.md.
add_library(zasmlib
    src/asmdasm/assembler.c
    src/asmdasm/dassembler.c
    src/asmdasm/compat.c
    src/asmdasm/tap.c
    src/asmdasm/ihex.c
    src/asmdasm/utils.c
    ${BISON_z80_parser_OUTPUTS}
    ${FLEX_z80_lexer_OUTPUTS})

target_include_directories(zasmlib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/asmdasm
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(zasmlib PUBLIC zdasm)
set_target_properties(zasmlib PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME zasm)

# the command line is a shell over the library
add_executable(zasm src/asmdasm/main.c)
target_link_libraries(zasm PRIVATE zasmlib)

if(WIN32)
    target_compile_definitions(zasmlib PUBLIC
        _WIN32_WINNT=0x0600
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE)
    enable_language(RC)
    target_sources(zasm PRIVATE src/app.rc)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    foreach(target zasm zasmlib zdasm)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
    endforeach()
    target_link_options(zasm PRIVATE -Wl,--gc-sections)

    # Each grammar rule renders its instruction into a fixed 64-byte buffer with
    # snprintf. The compiler cannot see that the %s arguments are register and
    # mnemonic names a few characters long, so it warns on all ~190 rules and
    # buries anything worth reading. A truncation could not corrupt the output
    # silently either: the shortened text would fail the opcode lookup and be
    # reported as an unknown mnemonic.
    #
    # The scanner and parser are machine-generated: their warnings are flex's
    # and bison's to answer for and vary by version, so -Werror does not apply
    # to them. Everything hand-written is held to it.
    set_source_files_properties(${BISON_z80_parser_OUTPUTS}
                                PROPERTIES COMPILE_OPTIONS "-Wno-format-truncation;-Wno-error")
    set_source_files_properties(${FLEX_z80_lexer_OUTPUTS}
                                PROPERTIES COMPILE_OPTIONS "-Wno-error")
endif()

# Before add_subdirectory: tests registered in a subdirectory are only picked
# up if testing was enabled in the top level first.
enable_testing()

# The Z80 CPU core: see docs/CPU-CORE-SPEC.md. Phase 0 is a walking skeleton -
# the engine and the interface are real, the instruction set is not there yet.
add_subdirectory(src/emucore/z80core)

# z80mon: an ImGui window onto the core - pins, clock control and a waveform.
# Off by default because it downloads Dear ImGui at configure time.
option(BUILD_Z80MON "Build the ImGui monitor for the CPU core" OFF)
if(BUILD_Z80MON)
    enable_language(CXX)
    add_subdirectory(tools/z80mon)
endif()

# monitor48k.asm is a listing of the real 16K ZX Spectrum ROM, so the assembled
# image must match monitor48k.rom byte for byte. This is the strongest
# regression test in the tree: it catches any layout or encoding change.
add_test(NAME rom_monitor48k
         COMMAND ${CMAKE_COMMAND}
                 -DZASM=$<TARGET_FILE:zasm>
                 -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/monitor48k/monitor48k.asm
                 -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/monitor48k.bin
                 -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/monitor48k/monitor48k.rom
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/rom_compare.cmake)
# Uses zasm.h and zdasm.h and nothing else, so it fails if the libraries stop
# being usable from outside the project.
add_executable(embed_test tests/embed_test.c)
target_link_libraries(embed_test PRIVATE zasmlib zdasm)
add_test(NAME embedding_api COMMAND embed_test)

add_test(NAME selftest_fallingblocks
         COMMAND zasm -s ${CMAKE_CURRENT_SOURCE_DIR}/tests/fallingblocks/fallingblocks.asm
                 -o fallingblocks.tap -x tap)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME opcode_suite
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py
                     --zasm $<TARGET_FILE:zasm>
                     --tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/asm_test.asm)

    add_test(NAME opcode_table_invariants
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_table.py
                     ${CMAKE_CURRENT_SOURCE_DIR}/src/asmdasm/z80tab.c)

    add_test(NAME ihex_output
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_ihex.py
                     --zasm $<TARGET_FILE:zasm>
                     --source ${CMAKE_CURRENT_SOURCE_DIR}/tests/monitor48k/monitor48k.asm
                     --reference ${CMAKE_CURRENT_SOURCE_DIR}/tests/monitor48k/monitor48k.rom)

    # every instruction the table claims must be reachable from the grammar
    add_test(NAME opcode_coverage
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_coverage.py
                     --zasm $<TARGET_FILE:zasm>
                     --table ${CMAKE_CURRENT_SOURCE_DIR}/src/asmdasm/z80tab.c)

    # disassemble the 16K ROM and assemble the listing back to the same bytes
    add_test(NAME round_trip_monitor48k
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/round_trip.py
                     --zasm $<TARGET_FILE:zasm>
                     --binary ${CMAKE_CURRENT_SOURCE_DIR}/tests/monitor48k/monitor48k.rom)
endif()
