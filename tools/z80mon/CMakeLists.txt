# z80mon - a window onto the CPU core: pins, clock control and a waveform.
#
# Windows only, because the platform layer is Win32 + WGL. That keeps the
# dependency list at "Dear ImGui" and nothing else: no GLFW, no SDL, no loader
# library, no runtime DLLs beyond what the OS already has.

if(NOT WIN32)
    message(STATUS "z80mon: skipped, the platform layer is Win32 only")
    return()
endif()

include(FetchContent)

# The -docking tags are the docking branch at a fixed point: dockable panels,
# and still a pinned revision rather than a moving branch.
FetchContent_Declare(imgui
                     GIT_REPOSITORY https://github.com/ocornut/imgui.git
                     GIT_TAG v1.92.9-docking
                     GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

# Dear ImGui ships as sources to compile into the application, so there is no
# imgui CMake target to link; build the core plus the two backends we use.
add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC opengl32 gdi32 dwmapi imm32)

add_executable(z80mon WIN32 main.cpp monitor.cpp z80mon.rc)

# so the .rc finds resources.h and logo.ico
target_include_directories(z80mon PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(z80mon PRIVATE imgui z80core zdasm comdlg32)

set_target_properties(z80mon PROPERTIES CXX_STANDARD 17
                                        CXX_STANDARD_REQUIRED ON
                                        # beside zasm rather than three directories down
                                        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # -Werror is not applied here: the warnings that matter are in our code and
    # the ones that are not are in a vendored dependency we do not control.
    target_compile_options(z80mon PRIVATE -Wall -Wextra)
endif()
