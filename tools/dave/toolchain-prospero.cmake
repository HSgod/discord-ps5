# CMake toolchain for the PS5 payload SDK (x86_64-sie-ps5 / prospero), used to
# cross-compile the DAVE dependency chain (T6.0-2).
#
# The compiler driver is the SDK wrapper, which already knows its -target,
# -isysroot, -isystem, -L and the emulated-TLS / no-PLT flags, so this file only
# has to stop CMake from probing by running binaries and from picking up host
# headers or host libraries.
#
# Passed as -DCMAKE_TOOLCHAIN_FILE=... together with
#   -DPS5_SDK=<payload sdk root>        (or the PS5_PAYLOAD_SDK environment)
#   -DPS5_DEPS_PREFIX=<install prefix>  (the prefix this chain installs into)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if (NOT DEFINED PS5_SDK AND DEFINED ENV{PS5_PAYLOAD_SDK})
    set(PS5_SDK "$ENV{PS5_PAYLOAD_SDK}")
endif ()
if (NOT PS5_SDK)
    message(FATAL_ERROR "PS5_SDK / PS5_PAYLOAD_SDK is unset")
endif ()

set(CMAKE_C_COMPILER "${PS5_SDK}/bin/prospero-clang")
set(CMAKE_CXX_COMPILER "${PS5_SDK}/bin/prospero-clang++")
set(CMAKE_AR "${PS5_SDK}/bin/llvm-ar")
set(CMAKE_RANLIB "${PS5_SDK}/bin/llvm-ranlib")
set(CMAKE_NM "${PS5_SDK}/bin/llvm-nm")
set(CMAKE_STRIP "${PS5_SDK}/bin/llvm-strip")

# Nothing produced here can be executed on the build host: probe by linking
# static libraries only.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${PS5_DEPS_PREFIX};${PS5_SDK}/target")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static text, no shared objects: everything ends up inside the .elf.
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
