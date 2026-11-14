#pragma once
// SGDX-02 Windows x64 narrow ABI fallback for an incomplete SDK header set.
// Prefer Microsoft's official <DbgHelp.h> when minidumpapiset.h exists.
// Otherwise this declares ONLY the three symbols/types used for crash-time
// MiniDumpWriteDump. DbgHelp.lib and DbgHelp.dll are still the real Microsoft
// binary import library and DLL, not a replacement implementation.
// ABI reference: Microsoft Learn, MiniDumpWriteDump, MINIDUMP_EXCEPTION_INFORMATION,
// MINIDUMP_TYPE; Win32 headers supply DWORD, HANDLE, EXCEPTION_POINTERS, BOOL.
#ifdef _WIN32
#include <windows.h>
#if defined(__has_include) && __has_include(<minidumpapiset.h>)
#include <DbgHelp.h>
#else
#include <cstddef>
typedef enum _MINIDUMP_TYPE {
    MiniDumpNormal = 0x00000000,
    MiniDumpWithThreadInfo = 0x00001000
} MINIDUMP_TYPE;
typedef struct _MINIDUMP_EXCEPTION_INFORMATION {
    DWORD ThreadId;
    PEXCEPTION_POINTERS ExceptionPointers;
    BOOL ClientPointers;
} MINIDUMP_EXCEPTION_INFORMATION, *PMINIDUMP_EXCEPTION_INFORMATION;
struct _MINIDUMP_USER_STREAM_INFORMATION;
struct _MINIDUMP_CALLBACK_INFORMATION;
extern "C" __declspec(dllimport) BOOL WINAPI MiniDumpWriteDump(
    HANDLE hProcess,
    DWORD ProcessId,
    HANDLE hFile,
    MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    struct _MINIDUMP_USER_STREAM_INFORMATION* UserStreamParam,
    struct _MINIDUMP_CALLBACK_INFORMATION* CallbackParam);
static_assert(offsetof(MINIDUMP_EXCEPTION_INFORMATION, ExceptionPointers)==8,
              "Windows x64 MiniDump exception info ABI unexpected");
static_assert(sizeof(MINIDUMP_EXCEPTION_INFORMATION)==24,
              "Windows x64 MiniDump exception info size unexpected");
#endif
#endif
