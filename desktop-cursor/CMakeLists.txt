cmake_minimum_required(VERSION 3.21)
project(lazer-cursor CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: one exe, nothing to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(lazer-cursor WIN32 main.cpp lazer-cursor.rc)
# The mod's easing curves, shared as is.
target_include_directories(lazer-cursor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/core)
target_compile_definitions(lazer-cursor PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(lazer-cursor PRIVATE windowscodecs dxgi xaudio2 shcore ole32 shell32 advapi32 user32 gdi32)
