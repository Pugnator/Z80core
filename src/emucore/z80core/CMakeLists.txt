# The Z80 CPU core: a plain C library, and the same code as a Lua 5.4 module.
#
# The C library builds anywhere with a C99 compiler and no dependencies. The
# Lua module additionally needs Lua headers, and on Windows something to
# resolve the lua_* symbols against - see docs/PHASE0.md.

add_library(z80core STATIC z80core.c)

target_include_directories(z80core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The same core as a shared library, for hosts that load it at runtime instead
# of linking it: the SingleStepTests harness drives it from Python through
# ctypes, and it is what an embedder on any platform would load.
add_library(z80core_shared SHARED z80core.c)
set_target_properties(z80core_shared PROPERTIES OUTPUT_NAME z80core)
target_include_directories(z80core_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(z80core_shared PRIVATE Z80CORE_SHARED Z80CORE_BUILD)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(z80core_shared PRIVATE -Wall -Wextra -Werror)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(z80core PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(z80bench bench/z80bench.c)
target_link_libraries(z80bench PRIVATE z80core)

add_executable(z80core_test tests/test_core.c)
target_link_libraries(z80core_test PRIVATE z80core)

# The engine and the instruction set are tested apart: one covers the clock,
# the pins and the fetch, the other what the instructions do and how long they
# take. A failure says which of the two is wrong without reading any output.
add_executable(z80core_instruction_test tests/test_instructions.c)
target_link_libraries(z80core_instruction_test PRIVATE z80core)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(z80bench PRIVATE -Wall -Wextra -Werror)
    target_compile_options(z80core_test PRIVATE -Wall -Wextra -Werror)
    target_compile_options(z80core_instruction_test PRIVATE -Wall -Wextra -Werror)
endif()

# The FUSE conformance suite: the first check on this core that was not
# written by the same person who wrote the core. See tests/fuse/README.md.
add_executable(z80core_fuse_test tests/fuse_test.c)
target_link_libraries(z80core_fuse_test PRIVATE z80core)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(z80core_fuse_test PRIVATE -Wall -Wextra -Werror)
endif()

# Frank Cringle's exerciser. It runs a program rather than a case, which is
# what makes it worth having alongside the other two - and it takes minutes
# rather than seconds, which is why it is off unless asked for. See
# tests/zex/README.md.
add_executable(z80core_zex_test tests/zex_test.c)
target_link_libraries(z80core_zex_test PRIVATE z80core)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(z80core_zex_test PRIVATE -Wall -Wextra -Werror)
endif()

option(Z80CORE_SLOW_TESTS "Register the exerciser suites, which take minutes" OFF)

add_test(NAME cpu_core COMMAND z80core_test)
add_test(NAME cpu_instructions COMMAND z80core_instruction_test)
add_test(NAME cpu_conformance_fuse
         COMMAND z80core_fuse_test
                 "${CMAKE_SOURCE_DIR}/tests/fuse/tests.in"
                 "${CMAKE_SOURCE_DIR}/tests/fuse/tests.expected")

if(Z80CORE_SLOW_TESTS)
    add_test(NAME cpu_conformance_zexdoc
             COMMAND z80core_zex_test "${CMAKE_SOURCE_DIR}/tests/zex/zexdoc.com")
    add_test(NAME cpu_conformance_zexall
             COMMAND z80core_zex_test "${CMAKE_SOURCE_DIR}/tests/zex/zexall.com")
    set_tests_properties(cpu_conformance_zexdoc cpu_conformance_zexall PROPERTIES TIMEOUT 1800)
endif()

# A short run, purely to prove the benchmark still works. The number it prints
# is for humans; nothing fails on being slow.
add_test(NAME cpu_core_benchmark COMMAND z80bench 1000000)

# ---------------------------------------------------------------------------
# The Lua module. Off unless Lua headers are pointed at, so the ordinary build
# never depends on having a Lua checkout.
# ---------------------------------------------------------------------------

set(Z80CORE_LUA_INCLUDE_DIR "" CACHE PATH "Directory containing lua.h (Lua 5.4)")
set(Z80CORE_LUA_LIBRARY "" CACHE FILEPATH
    "Library to resolve lua_* against: openvsm's import library, or lua54")

if(Z80CORE_LUA_INCLUDE_DIR)
    if(NOT EXISTS "${Z80CORE_LUA_INCLUDE_DIR}/lua.h")
        message(FATAL_ERROR "Z80CORE_LUA_INCLUDE_DIR has no lua.h: ${Z80CORE_LUA_INCLUDE_DIR}")
    endif()

    # MODULE, not SHARED: this is loaded by an interpreter, never linked against.
    add_library(z80core_lua MODULE z80core.c lua_z80core.c)

    # require("z80core") looks for a file named after the module
    set_target_properties(z80core_lua PROPERTIES OUTPUT_NAME "z80core" PREFIX "")

    target_include_directories(z80core_lua
                               PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${Z80CORE_LUA_INCLUDE_DIR})

    target_compile_definitions(z80core_lua PRIVATE Z80CORE_SHARED Z80CORE_BUILD)

    if(Z80CORE_LUA_LIBRARY)
        target_link_libraries(z80core_lua PRIVATE "${Z80CORE_LUA_LIBRARY}")
    elseif(WIN32)
        message(WARNING
                "No Z80CORE_LUA_LIBRARY given. On Windows the module must resolve lua_* "
                "against the interpreter that loads it, usually openvsm's import library. "
                "See docs/PHASE0.md.")
    endif()

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(z80core_lua PRIVATE -Wall -Wextra -Werror)
    endif()

    message(STATUS "z80core: building the Lua module against ${Z80CORE_LUA_INCLUDE_DIR}")
else()
    message(STATUS "z80core: Lua module skipped (set Z80CORE_LUA_INCLUDE_DIR to build it)")
endif()
