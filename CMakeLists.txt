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
    target_compile_options(zasm PRIVATE -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
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

# The emulator core is not built yet; enable once src/emucore grows up.
# add_executable(zemu src/emucore/cpu.cc src/emucore/exec.cc ...)

enable_testing()

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
