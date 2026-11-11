#pragma once
// SGDX-02 Windows opt-in crash-time minidump. Called ONLY by a separate
// debugger supervisor at a second-chance exception, before child termination.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "galaxy/ServerWindowsPrivate.hpp"
#include "galaxy/ServerWindowsMinidumpApi.hpp"
#include <sddl.h>
#include <aclapi.h>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <string>
#include <vector>
namespace galaxy::server_dx::windows_dump {
using ScopedPrivateSecurity=galaxy::server_dx::windows_private::ScopedSecurity;
inline bool capture_second_chance(
    const std::filesystem::path& save,
    const PROCESS_INFORMATION& process,
    const DEBUG_EVENT& event,
    std::wstring& saved_basename,
    std::string* failure=nullptr) noexcept{
    try {
        // These bounded diagnostic strings never contain a filesystem path,
        // crash memory or exception address. A failed dump still fails CLOSED.
        const auto fail=[&](const char* stage,DWORD win32)->bool {
            if(failure)*failure=std::string(stage)+":"+std::to_string(win32);
            return false;
        };
        ScopedPrivateSecurity security;
        if(!security.initialize())return fail("private_token_acl",GetLastError()); // no dump without private ACL
        std::filesystem::path directory=save;
        directory+=L".diagnostics";
        const std::wstring dir=directory.wstring();
        if(!security.protect_directory(dir))return fail("private_directory_acl",GetLastError());
        FILETIME timestamp{};
        GetSystemTimeAsFileTime(&timestamp);
        ULARGE_INTEGER number{};
        number.LowPart=timestamp.dwLowDateTime;
        number.HighPart=timestamp.dwHighDateTime;
        const std::wstring name=L"crash-"+std::to_wstring(number.QuadPart)+
            L"-"+std::to_wstring(process.dwProcessId)+L".dmp";
        const std::filesystem::path dest=directory/name;
        auto sa=security.attributes();
        HANDLE file=CreateFileW(dest.c_str(),GENERIC_WRITE,0,
            &sa,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file==INVALID_HANDLE_VALUE)return fail("private_file_create",GetLastError());

        CONTEXT context{};
        context.ContextFlags=CONTEXT_FULL;
        HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,
                                FALSE,event.dwThreadId);
        const bool has_context=thread && GetThreadContext(thread,&context);
        if(thread)CloseHandle(thread);
        EXCEPTION_RECORD record=event.u.Exception.ExceptionRecord;
        EXCEPTION_POINTERS pointers{};
        pointers.ExceptionRecord=&record;
        pointers.ContextRecord=&context;
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId=event.dwThreadId;
        info.ExceptionPointers=&pointers;
        info.ClientPointers=FALSE;
        // Some Windows DbgHelp installations cannot emit optional thread
        // info for a still-debugged crashing process. Preserve a real
        // second-chance dump by retrying once using Microsoft's minimal
        // MiniDumpNormal format while the original crash thread is HELD.
        // Never generate an after-exit fake dump, and never omit the private
        // security descriptor.
        BOOL written=MiniDumpWriteDump(process.hProcess,
            process.dwProcessId,file,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo),
            has_context?&info:nullptr,nullptr,nullptr);
        const DWORD first_error=written?ERROR_SUCCESS:GetLastError();
        DWORD retry_error=ERROR_SUCCESS;
        if(!written){
            const DWORD start=SetFilePointer(file,0,nullptr,FILE_BEGIN);
            if(start==INVALID_SET_FILE_POINTER || !SetEndOfFile(file)){
                retry_error=GetLastError();
            }else{
                written=MiniDumpWriteDump(process.hProcess,
                    process.dwProcessId,file,MiniDumpNormal,
                    nullptr,nullptr,nullptr);
                if(!written)retry_error=GetLastError();
            }
        }
        LARGE_INTEGER length{};
        const BOOL length_read=GetFileSizeEx(file,&length);
        const bool size_ok=length_read && length.QuadPart>=32 &&
                           length.QuadPart<=64LL*1024*1024;
        const DWORD size_error=length_read?ERROR_SUCCESS:GetLastError();
        CloseHandle(file);
        if(!written || !size_ok){
            DeleteFileW(dest.c_str());
            if(!written)
                return fail("dbghelp_threadinfo_then_normal",
                            retry_error?retry_error:first_error);
            return fail("dump_size_or_stat",size_error);
        }
        saved_basename=name;
        // Always cap retention, including repeated crash loops.
        std::vector<std::filesystem::path> dumps;
        for(const auto& item:std::filesystem::directory_iterator(directory)){
            if(item.is_regular_file() &&
               item.path().filename().wstring().rfind(L"crash-",0)==0 &&
               item.path().extension()==L".dmp")dumps.push_back(item.path());
        }
        std::sort(dumps.begin(),dumps.end());
        for(std::size_t i=0;dumps.size()>4 && i<dumps.size()-4;++i){
            std::error_code ec;
            std::filesystem::remove(dumps[i],ec);
        }
        return true;
    }catch(...){
        if(failure)*failure="dump_capture_exception";
        return false;
    }
}
} // namespace galaxy::server_dx::windows_dump
#endif
