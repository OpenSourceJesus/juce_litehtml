add_library(quickjs STATIC)

# quickjs-ng library sources (v0.16.x). Builtin *.h files are included from
# quickjs.c; win/ holds platform shims for MSVC builds.
set(quickjs_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/quickjs/dtoa.c
    ${CMAKE_CURRENT_SOURCE_DIR}/quickjs/libregexp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/quickjs/libunicode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/quickjs/quickjs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/quickjs/quickjs-libc.c
)

target_include_directories(quickjs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/quickjs
)

target_sources(quickjs PRIVATE ${quickjs_sources})

target_compile_definitions(quickjs
    PUBLIC
        QJS_BUILD_LIBC=1
    PRIVATE
        _GNU_SOURCE
        _SRC_SECURE_NO_WARNINGS
)

# ==============================================================================

add_library(litehtml STATIC)

file(GLOB_RECURSE src
    ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/src/*.cpp
)

target_include_directories(litehtml
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/include/litehtml
        ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/src
        ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/src/gumbo/include
        ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/src/gumbo/include/gumbo
)

if(MSVC)
    target_include_directories(litehtml
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/litehtml/src/gumbo/visualc/include
    )
endif()

target_compile_definitions(litehtml
    PUBLIC
        LITEHTML_UTF8=1
)

target_sources(litehtml PRIVATE ${src})

target_link_libraries(litehtml PUBLIC quickjs)
