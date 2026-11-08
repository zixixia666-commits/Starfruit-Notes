cmake_minimum_required(VERSION 3.22)
project(StarfruitNotes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(StarfruitNotes WIN32 src/main.cpp src/resources.rc)
set_target_properties(StarfruitNotes PROPERTIES OUTPUT_NAME "Starfruit Notes")
target_compile_definitions(StarfruitNotes PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(StarfruitNotes PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(StarfruitNotes PRIVATE comctl32 dwmapi shell32 advapi32)

enable_testing()
add_test(NAME StarfruitNotesSelfTest COMMAND "$<TARGET_FILE:StarfruitNotes>" --self-test)

install(TARGETS StarfruitNotes RUNTIME DESTINATION .)
