# SGDX-02 Windows x64 cross-build wrapper for user-owned Library cache v2.
# Hosts clang-cl/lld-link on Linux, but links with real Microsoft SDK .lib files.
# This file does NOT provide a Windows runtime.
if(NOT DEFINED ENV{SG_WINDOWS_TOOLROOT} OR "$ENV{SG_WINDOWS_TOOLROOT}" STREQUAL "")
  message(FATAL_ERROR "Set SG_WINDOWS_TOOLROOT to the restored v2 MSVC/Windows SDK cache")
endif()
include("$ENV{SG_WINDOWS_TOOLROOT}/portable/windows-msvc-sdk-clang.cmake")
if(SGDX_ALLOW_UNSUPPORTED_CLANG17)
  message(WARNING "SGDX Windows cross-build: unsupported Clang 17 / MSVC 14.44 STL combination; compiler/linker evidence ONLY, not production acceptance")
  string(APPEND CMAKE_CXX_FLAGS_INIT " /D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")
endif()
