#include "galaxy/ServerBreadcrumb.hpp"
#include "galaxy/ServerProcessDiagnostics.hpp"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif
int main(int argc,char** argv){
    if(argc<2)return 64;
    const std::string mode(argv[1]);
    if(mode=="worker-report" && argc==3){
        galaxy::server_dx::ProcessIncident report;
        report.kind="worker_failure";
        report.role="native_cell_worker";
        report.reason="preflight_failed";
        report.detail="original host denied native cell preflight";
        report.worker_scope="spatial/v2/s1,2,3";
        report.worker_epoch=47;
        report.last_committed_tick=99;
        report.last_world_generation=103;
        report.stderr_tail="real child rejected original worker task";
        return galaxy::server_dx::record_process_incident(argv[2],report)?0:81;
    }
    if(argc!=2)return 64;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    galaxy::server_dx::committed_breadcrumb(17,19);
    galaxy::server_dx::committed_breadcrumb(18,20);
    if(mode=="clean"){
        std::cout<<"SGVIEW|1|unmodified\n";
        return 0;
    }
    if(mode=="error"){
        std::cerr<<"fault-child: expected failure\n";
        return 7;
    }
    if(mode=="segv"){
#ifdef _WIN32
        RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,0,nullptr);
#else
        std::raise(SIGSEGV);
#endif
        return 40;
    }
    if(mode=="abort"){
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(),22);
#else
        std::raise(SIGABRT);
#endif
        return 40;
    }
    if(mode=="stderr"){
        std::cerr<<std::string(40000,'X')<<"TAIL-MARKER\n";
        return 21;
    }
    if(mode=="binary"){
        std::cerr<<"binary-tail:"<<static_cast<char>(0xff)
                 <<static_cast<char>(0x00)<<static_cast<char>(0x80)<<"\n";
        return 23;
    }
#ifndef _WIN32
    if(mode=="pid-hang"){
        std::cout<<"CHILD_PID="<<getpid()<<"\n"<<std::flush;
        for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif
    if(mode=="hang"){
        for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#ifdef _WIN32
    if(mode=="descendant"){
        wchar_t executable[MAX_PATH]{};
        const DWORD len=GetModuleFileNameW(nullptr,executable,MAX_PATH);
        if(!len || len>=MAX_PATH)return 72;
        std::wstring cmd=L"\"" + std::wstring(executable) + L"\" hang";
        std::vector<wchar_t> mutable_cmd(cmd.begin(),cmd.end());
        mutable_cmd.push_back(L'\0');
        STARTUPINFOW si{};si.cb=sizeof(si);
        PROCESS_INFORMATION child{};
        if(!CreateProcessW(executable,mutable_cmd.data(),nullptr,nullptr,
             FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&child))return 72;
        std::cout<<"DESCENDANT="<<child.dwProcessId<<"\n"<<std::flush;
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
        for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#else
    if(mode=="descendant"){
        const pid_t child=fork();
        if(child==0){
            for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(child<0)return 72;
        std::cout<<"DESCENDANT="<<child<<"\n"<<std::flush;
        for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif
    return 65;
}
