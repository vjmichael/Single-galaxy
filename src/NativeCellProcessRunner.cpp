#include "galaxy/NativeCellProcessRunner.hpp"
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace galaxy::spatial {
namespace {
std::string hex_worker(const std::string& text) {
    constexpr char digits[]="0123456789abcdef";
    std::string hex;
    hex.reserve(text.size()*2);
    for(const unsigned char c:text){
        hex.push_back(digits[c>>4]);
        hex.push_back(digits[c&15]);
    }
    return hex;
}
bool safe_scope(const std::string& text) {
    if(text.empty() || text.size()>256 ||
       (text.rfind("spatial/v1/s",0)!=0 &&
        text.rfind("spatial/v2/s",0)!=0))return false;
    for(const char ch:text)
        if(!((ch>='0'&&ch<='9')||(ch>='A'&&ch<='Z')||
             (ch>='a'&&ch<='z')||ch=='/'||ch==','||
             ch=='-'||ch=='_'))return false;
    return true;
}
std::string expected_output(const CellProcessTask& task){
    const auto score=task.physical_ships*
        (static_cast<std::uint64_t>(task.detail)+1);
    return "SGCELL1|"+task.scope+"|"+hex_worker(task.worker)+"|"+
        std::to_string(task.original_generation)+"|"+
        std::to_string(task.original_hash)+"|"+
        std::to_string(task.physical_ships)+"|"+
        std::to_string(task.detail)+"|"+std::to_string(score)+"\n";
}
#ifdef _WIN32
std::wstring utf16(const std::string& input){
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        input.c_str(),-1,nullptr,0);
    if(count<=0)throw std::runtime_error("invalid native executable path");
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.c_str(),
                           -1,result.data(),count))
        throw std::runtime_error("cannot decode native executable path");
    result.pop_back();return result;
}
bool execute(const std::string& executable,const CellProcessTask& task,
    std::chrono::milliseconds deadline,std::string& output) {
    const auto binary=utf16(executable);
    if(binary.empty() || binary.find(L'"')!=std::wstring::npos)return false;
    const auto args=std::string("--cell-read-v1 ")+task.scope+" "+
        hex_worker(task.worker)+" "+std::to_string(task.original_generation)+" "+
        std::to_string(task.original_hash)+" "+
        std::to_string(task.physical_ships)+" "+
        std::to_string(task.detail);
    const auto cmd=L"\""+binary+L"\" "+utf16(args);
    std::vector<wchar_t> writable(cmd.begin(),cmd.end());
    writable.push_back(L'\0');
    SECURITY_ATTRIBUTES attrs{};
    attrs.nLength=sizeof(attrs);attrs.bInheritHandle=TRUE;
    HANDLE readpipe=nullptr,writepipe=nullptr,job=nullptr;
    PROCESS_INFORMATION child{};
    const auto close=[](HANDLE& handle) {
        if(handle){CloseHandle(handle);handle=nullptr;}
    };
    if(!CreatePipe(&readpipe,&writepipe,&attrs,0))return false;
    if(!SetHandleInformation(readpipe,HANDLE_FLAG_INHERIT,0)){
        close(readpipe);close(writepipe);return false;
    }
    job=CreateJobObjectW(nullptr,nullptr);
    if(!job){close(readpipe);close(writepipe);return false;}
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit=256ULL*1024*1024;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,
                               &limits,sizeof(limits))){
        close(job);close(readpipe);close(writepipe);return false;
    }
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdOutput=writepipe;
    startup.hStdError=writepipe;
    startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    const bool started=CreateProcessW(binary.c_str(),writable.data(),
        nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,
        nullptr,nullptr,&startup,&child)!=FALSE;
    close(writepipe);
    if(!started){close(job);close(readpipe);return false;}
    const bool assigned=AssignProcessToJobObject(job,child.hProcess)!=FALSE;
    if(!assigned){
        TerminateProcess(child.hProcess,1);
        WaitForSingleObject(child.hProcess,INFINITE);
        close(child.hThread);close(child.hProcess);
        close(job);close(readpipe);return false;
    }
    if(ResumeThread(child.hThread)==static_cast<DWORD>(-1)){
        TerminateJobObject(job,1);
        WaitForSingleObject(child.hProcess,INFINITE);
        close(child.hThread);close(child.hProcess);
        close(job);close(readpipe);return false;
    }
    close(child.hThread);
    const DWORD wait=WaitForSingleObject(child.hProcess,
        static_cast<DWORD>(deadline.count()));
    DWORD code=1;
    const bool success=wait==WAIT_OBJECT_0 &&
        GetExitCodeProcess(child.hProcess,&code) && code==0;
    if(!success)TerminateJobObject(job,1);
    WaitForSingleObject(child.hProcess,INFINITE);
    // Closing the job terminates any descendant retaining stdout. No child
    // can prolong the caller's post-exit read by keeping its pipe open.
    close(job);
    close(child.hProcess);
    if(success){
        char buffer[2048]{};
        DWORD received=0;
        if(ReadFile(readpipe,buffer,static_cast<DWORD>(sizeof(buffer)),
                    &received,nullptr))
            output.assign(buffer,buffer+received);
    }
    close(readpipe);
    return success;
}
#else
bool execute(const std::string& executable,const CellProcessTask& task,
    std::chrono::milliseconds deadline,std::string& output){
    int fds[2]={-1,-1};
    if(pipe(fds)!=0)return false;
    const auto worker=hex_worker(task.worker);
    const auto generation=std::to_string(task.original_generation);
    const auto checksum=std::to_string(task.original_hash);
    const auto ships=std::to_string(task.physical_ships);
    const auto detail=std::to_string(task.detail);
    const pid_t pid=fork();
    if(pid<0){close(fds[0]);close(fds[1]);return false;}
    if(pid==0){
        close(fds[0]);
        if(setpgid(0,0)!=0 || dup2(fds[1],STDOUT_FILENO)<0 ||
           dup2(fds[1],STDERR_FILENO)<0)_exit(125);
        close(fds[1]);
        const rlimit address{256ULL*1024*1024,256ULL*1024*1024};
        const rlimit cpu{2,2};
        if(setrlimit(RLIMIT_AS,&address)!=0 ||
           setrlimit(RLIMIT_CPU,&cpu)!=0)_exit(125);
        const char* const args[]={executable.c_str(),"--cell-read-v1",
            task.scope.c_str(),worker.c_str(),generation.c_str(),
            checksum.c_str(),ships.c_str(),detail.c_str(),nullptr};
        execv(executable.c_str(),const_cast<char* const*>(args));
        _exit(127);
    }
    close(fds[1]);
    // Do not allow a race with parent->child process group creation.
    (void)setpgid(pid,pid);
    const auto until=std::chrono::steady_clock::now()+deadline;
    int status=0;
    bool exited=false;
    while(std::chrono::steady_clock::now()<until){
        const auto observed=waitpid(pid,&status,WNOHANG);
        if(observed==pid){exited=true;break;}
        if(observed<0 && errno!=EINTR)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(!exited){
        (void)kill(-pid,SIGKILL);
        (void)kill(pid,SIGKILL);
        while(waitpid(pid,&status,0)<0 && errno==EINTR){}
    }else{
        // Any grandchildren of the selected worker are also terminated.
        (void)kill(-pid,SIGKILL);
    }
    const bool success=exited && WIFEXITED(status) && WEXITSTATUS(status)==0;
    if(success){
        char buffer[2048];
        const auto size=read(fds[0],buffer,sizeof(buffer));
        if(size>0)output.assign(buffer,static_cast<std::size_t>(size));
    }
    close(fds[0]);
    return success;
}
#endif
} // namespace

CellProcessOutcome run_cell_process(const std::string& executable,
    const CellProcessTask& task,std::chrono::milliseconds deadline) {
    if(executable.empty() || task.worker.empty() || task.worker.size()>128 ||
       !safe_scope(task.scope) || task.original_generation==0 ||
       task.physical_ships==0 || task.physical_ships>100000 ||
       task.detail>2 || deadline.count()<1 || deadline.count()>30000)
        return {false,"invalid bounded read-only worker admission"};
    try{
        std::string actual;
        if(!execute(executable,task,deadline,actual))
            return {false,"OS worker failed, timed out or was terminated"};
        if(actual!=expected_output(task))
            return {false,"worker returned stale or forged original-world report"};
        return {true,{}};
    }catch(const std::exception& e){
        return {false,e.what()};
    }
}
} // namespace galaxy::spatial
