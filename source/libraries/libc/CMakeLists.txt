#[[
    CMakeLists.txt - build system for NexNix libc
    Copyright 2023 - 2024 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
]]

cmake_minimum_required(VERSION 3.7)
project(libc VERSION 0.0.1)
enable_language(C ASM ASM_NASM)
set(CMAKE_C_STANDARD 99)

# Various options
option(BUILD_SHARED_LIBS "Specifies if shared libraries should be built" OFF)
option(LIBC_ENABLE_TESTS "Specifies if the test suite should be built" OFF)
option(LIBC_LIBK_ONLY "Build libc as libk" OFF)

if(${LIBC_ENABLE_TESTS} AND NOT ${CMAKE_CROSSCOMPILING})
    enable_testing()
endif()

# Include modules
include(GNUInstallDirs)
include(NexTest)
include(SdkCompilerTest)

# See if tests should be enabled
if(${LIBC_ENABLE_TESTS})
    nextest_enable_tests()
endif()

# Sources used for libc and libk
list(APPEND LIBC_SOURCE_ALWAYS
    string/memset.c
    string/memcmp.c
    string/memcpy.c
    string/strcmp.c
    string/strlen.c
    string/strcpy.c
    string/strcat.c
    string/atoi.c
    stdio/vsnprintf.c
    stdio/vsprintf.c
    stdio/sprintf.c
    stdio/snprintf.c
    stdio/vprintf_core.c)

list(APPEND LIBC_SOURCE_HOSTED
    entry/cmain.c
    stdlib/exit.c)

include(arch/${NEXNIX_ARCH}/arch.cmake)

# Headers used on libc and libk
list(APPEND LIBC_HEADERS_ALWAYS
    include/string.h
    include/uchar.h
    include/stdlib.h
    include/stddef.h
    include/stdbool.h
    include/stdnoreturn.h
    include/stdio.h
    include/stdarg.h
    include/elf.h
    include/assert.h
    include/errno.h
    include/unistd.h
    include/time.h)
list(APPEND LIBC_SYS_HEADERS_ALWAYS
    include/sys/types.h)
list(APPEND LIBC_ARCH_HEADERS_BITS
    arch/${NEXNIX_ARCH}/include/bits/arch.h)
list(APPEND LIBC_ARCH_HEADERS
    arch/${NEXNIX_ARCH}/include/stdint.h)
list(APPEND LIBC_HEADERS_BITS
    include/bits/types.h)

# Build and install everything
if(NOT LIBC_LIBK_ONLY)
    set(LIBC_TARGET c)
    add_library(c ${LIBC_SOURCE_ALWAYS} ${LIBC_SOURCE_HOSTED} ${LIBC_SOURCE_ARCH_HOSTED})
    # Compile crt0. We make it an object library
    add_library(crt0 OBJECT ${LIBC_SOURCE_CRT0})
    install(TARGETS c DESTINATION ${CMAKE_INSTALL_LIBDIR})
    # Install crt0
    install(FILES $<TARGET_OBJECTS:crt0> TYPE LIB RENAME crt0.o)
else()
    set(LIBC_TARGET k)
    add_library(k ${LIBC_SOURCE_ALWAYS})

    # If buliding as libk, compile as PIE
    target_compile_options(k PRIVATE -fpie)
    install(TARGETS k DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(${LIBC_TARGET} PRIVATE include)
target_include_directories(${LIBC_TARGET} PRIVATE arch/${NEXNIX_ARCH}/include)

install(FILES ${LIBC_HEADERS_ALWAYS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${LIBC_ARCH_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${LIBC_HEADERS_BITS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bits)
install(FILES ${LIBC_ARCH_HEADERS_BITS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bits)
install(FILES ${LIBC_SYS_HEADERS_ALWAYS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sys)
