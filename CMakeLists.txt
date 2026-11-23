cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
project(JumpingClock VERSION 0.1.1 LANGUAGES CXX RC)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(JumpingClockCore STATIC
    src/clock_data.cpp
    src/cities.cpp
    src/probe.cpp
)
target_include_directories(JumpingClockCore PUBLIC src)
target_compile_features(JumpingClockCore PUBLIC cxx_std_20)
target_compile_definitions(JumpingClockCore PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_compile_options(JumpingClockCore PUBLIC
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>"
)
target_link_libraries(JumpingClockCore PRIVATE user32 advapi32 iphlpapi ws2_32)

add_executable(JumpingClock WIN32
    src/main.cpp
    src/settings_dialog.cpp
    src/JumpingClock.rc
)
set_source_files_properties(src/JumpingClock.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/JumpingClock.manifest"
)
target_link_options(JumpingClock PRIVATE /MANIFEST:NO)
target_link_libraries(JumpingClock PRIVATE JumpingClockCore user32 gdi32 shell32 comctl32)

include(CTest)
if(BUILD_TESTING)
    add_executable(JumpingClockSmoke tests/smoke.cpp)
    target_link_libraries(JumpingClockSmoke PRIVATE JumpingClockCore user32 advapi32)
    add_test(NAME JumpingClockSmoke COMMAND JumpingClockSmoke)
    set_tests_properties(JumpingClockSmoke PROPERTIES TIMEOUT 20)
endif()
