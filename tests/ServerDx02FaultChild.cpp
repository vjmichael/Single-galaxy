#include "galaxy/ServerBreadcrumb.hpp"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif
int main(int argc,char** argv){
    if(argc!=2)return 64;
    const std::string mode(argv[1]);
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
#ifndef _WIN32
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
