#pragma once
// SGDX-02 Windows opt-in crash-time minidump. Called ONLY by a separate
// debugger supervisor at a second-chance exception, before child termination.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "galaxy/ServerWindowsPrivate.hpp"
#include <DbgHelp.h>
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
    std::wstring& saved_basename) noexcept{
    try {
        ScopedPrivateSecurity security;
        if(!security.initialize())return false; // no dump without private ACL
        std::filesystem::path directory=save;
        directory+=L".diagnostics";
        const std::wstring dir=directory.wstring();
        if(!security.protect_directory(dir))return false;
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
        if(file==INVALID_HANDLE_VALUE)return false;

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
        const BOOL written=MiniDumpWriteDump(process.hProcess,
            process.dwProcessId,file,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo),
            has_context?&info:nullptr,nullptr,nullptr);
        LARGE_INTEGER length{};
        const bool size_ok=GetFileSizeEx(file,&length) &&
            length.QuadPart>=32 && length.QuadPart<=64LL*1024*1024;
        CloseHandle(file);
        if(!written || !size_ok){
            DeleteFileW(dest.c_str());return false;
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
    }catch(...){return false;}
}
} // namespace galaxy::server_dx::windows_dump
#endif
