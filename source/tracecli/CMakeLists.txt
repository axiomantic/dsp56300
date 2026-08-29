cmake_minimum_required(VERSION 3.10)

project(dsp56kTraceCli)

add_executable(dsp56kTraceCli)

target_sources(dsp56kTraceCli PRIVATE main.cpp)

target_link_libraries(dsp56kTraceCli PRIVATE dsp56kEmu)

# The memory-write watch records ride the DebuggerInterface hooks that
# Memory::dspWrite/get call under DSP56300_DEBUGGER=1. Without the macro the
# hooks are compiled out and a watch list is silently blind.
target_compile_definitions(dsp56kTraceCli PRIVATE DSP56300_DEBUGGER=1)
