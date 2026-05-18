/*
 * version.h — Firmware version definitions
 *
 * FW_VERSION and GIT_HASH are injected by CMakeLists.txt via
 * target_compile_definitions(). The fallbacks below apply only when
 * building outside of CMake (e.g. direct gcc invocation or an IDE
 * that does not run CMake first).
 */
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "v?.?-unknown"
#endif

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif
