# The Z80 CPU core: a plain C library, and the same code as a Lua 5.4 module.
#
# The C library builds anywhere with a C99 compiler and no dependencies. The
# Lua module additionally needs Lua headers, and on Windows something to
# resolve the lua_* symbols against - see docs/PHASE0.md.

add_library(z80core STATIC z80core.c)

target_include_directories(z80core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(z80core PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(z80bench bench/z80bench.c)
target_link_libraries(z80bench PRIVATE z80core)

add_executable(z80core_test tests/test_core.c)
target_link_libraries(z80core_test PRIVATE z80core)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(z80bench PRIVATE -Wall -Wextra -Werror)
    target_compile_options(z80core_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME cpu_core COMMAND z80core_test)

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
