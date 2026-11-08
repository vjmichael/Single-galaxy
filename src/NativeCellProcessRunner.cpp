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
#ifdef __linux__
#include <cerrno>
#include <cstddef>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
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
    // Never inherit arbitrary original host/SQLite/IPC handles.
    // Windows' explicit handle-list attribute permits only a private
    // read-only NUL input and this child's bounded report output pipe.
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,&attrs,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(input==INVALID_HANDLE_VALUE){
        close(job);close(readpipe);close(writepipe);return false;
    }
    SIZE_T bytes=0;
    (void)InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    if(!bytes){
        close(input);close(job);close(readpipe);close(writepipe);return false;
    }
    std::vector<unsigned char> backing(bytes);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb=sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput=writepipe;
    startup.StartupInfo.hStdError=writepipe;
    startup.StartupInfo.hStdInput=input;
    startup.lpAttributeList=
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(backing.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)){
        close(input);close(job);close(readpipe);close(writepipe);return false;
    }
    HANDLE allowed[2]={writepipe,input};
    const bool restricted=UpdateProcThreadAttribute(startup.lpAttributeList,
        0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(allowed),
        nullptr,nullptr)!=FALSE;
    const bool started=restricted &&
        CreateProcessW(binary.c_str(),writable.data(),nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,
            nullptr,nullptr,&startup.StartupInfo,&child)!=FALSE;
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    close(input);
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
        // A grandchild holding a duplicate stdout handle must never make
        // report collection exceed the completed worker's deadline.
        // Peek the already-buffered bytes; never issue a blocking read for
        // data that might arrive only after a descendant's future exit.
        DWORD available=0;
        if(PeekNamedPipe(readpipe,nullptr,0,nullptr,&available,nullptr) &&
           available>0 && available<=2048){
            char buffer[2048]{};
            DWORD received=0;
            if(ReadFile(readpipe,buffer,available,&received,nullptr) &&
               received==available)
                output.assign(buffer,buffer+received);
        }
    }
    close(readpipe);
    return success;
}
#else
#ifdef __linux__
// This component runs exactly one read-only native report task per child.
// Descendants are not part of its protocol, so deny process creation and
// process-group escape inside the kernel before the child can exec. These
// filters are inherited across execve; failure to apply is fatal.
bool restrict_native_worker_descendants(){
    constexpr std::uint32_t denied=SECCOMP_RET_ERRNO |
        static_cast<std::uint32_t>(EPERM);
    const sock_filter filter[]={
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
            static_cast<std::uint32_t>(offsetof(seccomp_data,nr))),
#ifdef SYS_clone
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_clone,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
#ifdef SYS_clone3
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_clone3,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
#ifdef SYS_fork
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_fork,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
#ifdef SYS_vfork
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_vfork,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
#ifdef SYS_setsid
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_setsid,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
#ifdef SYS_setpgid
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_setpgid,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
#ifdef SYS_unshare
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_unshare,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
#ifdef SYS_setns
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_setns,0,1),
        BPF_STMT(BPF_RET|BPF_K,denied),
#endif
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)
    };
    sock_fprog program{static_cast<unsigned short>(
        sizeof(filter)/sizeof(filter[0])),const_cast<sock_filter*>(filter)};
    return prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0 &&
        prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program)==0;
}
#endif
bool execute(const std::string& executable,const CellProcessTask& task,
    std::chrono::milliseconds deadline,std::string& output){
    int fds[2]={-1,-1};
    if(pipe(fds)!=0)return false;
    const auto worker=hex_worker(task.worker);
    const auto generation=std::to_string(task.original_generation);
    const auto checksum=std::to_string(task.original_hash);
    const auto ships=std::to_string(task.physical_ships);
    const auto detail=std::to_string(task.detail);
    // Precompute the descriptor ceiling in the parent. The child is forked
    // from a multi-threaded supervisor and must not call sysconf or use
    // libc allocation/locks before its execv.
    const long descriptor_limit=sysconf(_SC_OPEN_MAX);
    const pid_t launching_parent=getpid();
    const pid_t pid=fork();
    if(pid<0){close(fds[0]);close(fds[1]);return false;}
    if(pid==0){
        close(fds[0]);
        if(setpgid(0,0)!=0 || dup2(fds[1],STDOUT_FILENO)<0 ||
           dup2(fds[1],STDERR_FILENO)<0)_exit(125);
        close(fds[1]);
        // The forked worker must NEVER exec with an inherited original
        // SQLite/socket/host-authorization descriptor. Prefer Linux kernel
        // close_range; on other POSIX kernels use a strictly bounded,
        // fail-closed descriptor sweep before exec.
        bool stripped=false;
#if defined(__linux__) && defined(SYS_close_range)
        stripped=syscall(SYS_close_range,3U,~0U,0U)==0;
#endif
        if(!stripped){
            if(descriptor_limit<3 || descriptor_limit>65536)_exit(125);
            for(int fd=3;fd<descriptor_limit;++fd)(void)close(fd);
        }
        const rlimit address{256ULL*1024*1024,256ULL*1024*1024};
        const rlimit cpu{2,2};
        if(setrlimit(RLIMIT_AS,&address)!=0 ||
           setrlimit(RLIMIT_CPU,&cpu)!=0)_exit(125);
#ifdef __linux__
        // Closing the parent unexpectedly cannot orphan an uncontained
        // read-only worker. PR_SET_PDEATHSIG is checked against the
        // captured original parent PID to close the post-fork race.
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)!=0 ||
           getppid()!=launching_parent ||
           !restrict_native_worker_descendants())_exit(125);
#endif
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
        // The direct child can exit while an out-of-group descendant keeps
        // the inherited stdout pipe open. A plain read() would then block
        // indefinitely despite our verified leader-exit deadline.
        const int flags=fcntl(fds[0],F_GETFL,0);
        if(flags>=0 && fcntl(fds[0],F_SETFL,flags|O_NONBLOCK)==0){
            char buffer[2049]{};
            const auto size=read(fds[0],buffer,sizeof(buffer));
            if(size>0 && size<=2048)
                output.assign(buffer,static_cast<std::size_t>(size));
        }
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
