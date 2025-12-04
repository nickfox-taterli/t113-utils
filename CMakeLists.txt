cmake_minimum_required(VERSION 3.10)

# Global toolchain / compile flags for all RISC-V firmware targets
set(RISCV_ROOT_PATH "/home/taterli/sdk/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.8.1")

set(RISCV_ROOT_PATH ${RISCV_ROOT_PATH} CACHE STRING "root path to riscv toolchain")

set(CMAKE_C_COMPILER "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-gnu-g++")

set(CMAKE_SYSROOT "${RISCV_ROOT_PATH}/sysroot")

set(CMAKE_COMMON_FLAGS "-mcpu=c906fdv -mcmodel=medany -fno-stack-protector")
set(CMAKE_DISABLE_WARN_FLAGS "-Wint-to-pointer-cast -Wimplicit-function-declaration")

set(CMAKE_C_FLAGS "${CMAKE_DISABLE_WARN_FLAGS} ${CMAKE_C_FLAGS} ${CMAKE_COMMON_FLAGS}" CACHE STRING "c flags")
set(CMAKE_CXX_FLAGS "${CMAKE_DISABLE_WARN_FLAGS} ${CMAKE_CXX_FLAGS} ${CMAKE_COMMON_FLAGS}" CACHE STRING "c++ flags")

set(CMAKE_FIND_ROOT_PATH "${RISCV_ROOT_PATH}/riscv64-unknown-linux-gnu")

set(CMAKE_SYSROOT "${RISCV_ROOT_PATH}/sysroot")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Set Version, Release need cmake
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)

# use git version as library version (shared by all sub-firmware)
find_package(Git QUIET)
if (Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.git")
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE _git_version
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
            COMMAND ${GIT_EXECUTABLE} log -1 --pretty=format:%h
            OUTPUT_VARIABLE PROJECT_GIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            WORKING_DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
else ()
    set(_git_version 0)
    set(PROJECT_GIT_HASH "")
endif ()

project(T113-I_Miniloader C CXX ASM)

set(CMAKE_AR "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-gnu-ar")
set(CMAKE_OBJCOPY "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-gnu-objcopy")

# C906 firmware is now in its own subdirectory.
# Later HIFI4 or other cores can be added similarly.
add_subdirectory(c906)
