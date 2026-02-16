cmake_minimum_required(VERSION 3.20)
project(claudeusage C)

set(CMAKE_C_STANDARD 11)

add_executable(claudeusage WIN32
    src/main.c
    src/http.c
    src/api.c
    src/popup.c
    src/config.c
    src/util.c
    vendor/cJSON.c
    res/app.rc
)

target_include_directories(claudeusage PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor
)

target_compile_definitions(claudeusage PRIVATE
    UNICODE
    _UNICODE
    NTDDI_VERSION=0x06010000
    _WIN32_WINNT=0x0601
    WINVER=0x0601
)

target_compile_options(claudeusage PRIVATE -Wall -Wextra -O2)

target_link_libraries(claudeusage PRIVATE
    winhttp
    shell32
    user32
    gdi32
    comctl32
    kernel32
    shlwapi
    ole32
)

target_link_options(claudeusage PRIVATE -mwindows -static -municode)
