cmake_minimum_required(VERSION 3.14)
project(liblsd VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------- Options ----------
option(LSD_BUILD_TESTS "Build tests" ON)
option(BUILD_SHARED_LIBS "Build shared library" OFF)

# ---------- Dependencies ----------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(ZLIB REQUIRED)
find_package(VorbisFile REQUIRED)
find_package(Unistring REQUIRED)

# dictzip (submodule)
set(DICTZIP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(NOT TARGET dictzip)
    add_subdirectory(deps/dictzip)
endif()

# ---------- Library ----------
file(GLOB LSD_SOURCES src/*.c)

set(LSD_PUBLIC_HEADERS
    src/lsd_reader.h
    src/lsd_types.h
    src/dsl_reader.h
    src/lsa_reader.h
)

set(LSD_PRIVATE_HEADERS
    src/lsd_page_store.h
    src/lsd_bitstream.h
    src/lsd_decoder.h
    src/lsd_huffman.h
    src/lsd_utils.h
)

add_library(lsd ${LSD_SOURCES} ${LSD_PUBLIC_HEADERS} ${LSD_PRIVATE_HEADERS})
target_include_directories(lsd
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include/lsd>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(lsd PUBLIC ZLIB::ZLIB VorbisFile::VorbisFile Unistring::Unistring dictzip)

# Platform-specific link libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(lsd PUBLIC m)
endif()

# MSVC: suppress POSIX function name warnings (strdup, etc.)
if(MSVC)
    target_compile_definitions(lsd PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )
    target_compile_options(lsd PRIVATE /W4)
else()
    target_compile_options(lsd PRIVATE -Wall -Wextra -pedantic)
endif()

# ---------- Tests ----------
if(LSD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------- Install ----------
include(GNUInstallDirs)

install(TARGETS lsd
    EXPORT lsdTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${LSD_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lsd
)

install(EXPORT lsdTargets
    FILE lsdTargets.cmake
    NAMESPACE lsd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lsd
)
