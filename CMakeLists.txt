cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
project(JumpingClock LANGUAGES CXX RC)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_executable(JumpingClock WIN32
    src/main.cpp
    src/clock_data.cpp
    src/probe.cpp
    src/settings_dialog.cpp
    src/JumpingClock.rc
)
set_source_files_properties(src/JumpingClock.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/JumpingClock.manifest"
)
target_compile_features(JumpingClock PRIVATE cxx_std_20)
target_compile_definitions(JumpingClock PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_compile_options(JumpingClock PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>"
)
set_property(TARGET JumpingClock PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
target_link_options(JumpingClock PRIVATE /MANIFEST:NO)
target_link_libraries(JumpingClock PRIVATE
    user32 gdi32 shell32 advapi32 iphlpapi ws2_32
)
