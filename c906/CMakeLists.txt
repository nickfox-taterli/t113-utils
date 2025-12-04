cmake_minimum_required(VERSION 3.10)

# When configured from this directory directly (standalone build),
# we must also set up the RISC-V toolchain and project here.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(RISCV_ROOT_PATH "/home/taterli/sdk/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.8.1"
        CACHE STRING "root path to riscv toolchain")

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

    project(C906_Firmware C CXX ASM)

    set(CMAKE_AR "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-gnu-ar")
    set(CMAKE_OBJCOPY "${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-gnu-objcopy")
endif()

# Configure generated config header for C906 firmware
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)

# C906-specific include paths
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/sys"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

# Linker script for C906 firmware
set(C906_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/link.ld")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T ${C906_LINKER_SCRIPT} -nostdlib")

add_subdirectory(lib)
add_subdirectory(src)

