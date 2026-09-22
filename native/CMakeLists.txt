cmake_minimum_required(VERSION 3.25)

project(helltime_native VERSION 0.1.0 LANGUAGES CXX)

add_executable(helltime WIN32
    src/main.cpp
    src/domain/safety.cpp
    src/domain/schedule.cpp
    src/domain/settings.cpp
    src/ui/main_window.cpp
    src/integration/audio.cpp
    src/integration/overlay.cpp
    src/integration/app.cpp
)

target_compile_features(helltime PRIVATE cxx_std_20)
target_include_directories(helltime PRIVATE src)
target_compile_definitions(helltime PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_link_libraries(helltime PRIVATE
    d2d1 dwrite windowscodecs
    ole32 uuid sapi winmm shell32 user32 gdi32 comctl32 dwmapi
)

if(MSVC)
    # Portable release binary: link the MSVC runtime statically.
    set_property(TARGET helltime PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(helltime PRIVATE /W4 /permissive- /utf-8)
    target_link_options(helltime PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# Keep generated binaries in the native build tree, never in dist/.
set_target_properties(helltime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)

enable_testing()
add_executable(helltime_domain_tests
    tests/domain_tests.cpp
    src/domain/safety.cpp
    src/domain/schedule.cpp
    src/domain/settings.cpp
)
target_include_directories(helltime_domain_tests PRIVATE src)
target_compile_features(helltime_domain_tests PRIVATE cxx_std_20)
target_compile_definitions(helltime_domain_tests PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
)
target_link_libraries(helltime_domain_tests PRIVATE ole32 user32)
if(MSVC)
    set_property(TARGET helltime_domain_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(helltime_domain_tests PRIVATE /W4 /permissive- /utf-8)
endif()
add_test(NAME helltime_domain_tests COMMAND helltime_domain_tests)
