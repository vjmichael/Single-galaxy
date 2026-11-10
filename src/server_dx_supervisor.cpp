// SGDX-02: independent game/worker process supervisor, no game SQLite writer.
// Stdout is always the child's original unmodified game/client frame stream.
#include "galaxy/ServerProcessDiagnostics.hpp"
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "galaxy/ServerWindowsMinidump.hpp"
#else
#include <cerrno>
#include <charconv>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace dx=galaxy::server_dx;
using Clock=std::chrono::steady_clock;
struct Args {
    std::string save,role,build_sha;
    int timeout_ms=60000;
    bool record_clean=false,allow_core=false,windows_minidump=false,interactive=false;
    std::vector<std::string> command;
};
static Args parse(int argc,char** argv){
    Args a;
    int i=1;
    for(;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--") {++i;break;}
        if((arg=="--save" || arg=="--role" || arg=="--build-sha" || arg=="--timeout-ms") && i+1<argc){
            const std::string value=argv[++i];
            if(arg=="--save")a.save=value;
            else if(arg=="--role")a.role=value;
            else if(arg=="--build-sha")a.build_sha=value;
            else {
                if(value.empty() || value.find_first_not_of("0123456789")!=std::string::npos)
                    throw std::invalid_argument("timeout must be decimal");
                const auto value_long=std::stoll(value);
                if(value_long<1 || value_long>3600000)throw std::invalid_argument("timeout out of range");
                a.timeout_ms=static_cast<int>(value_long);
            }
        }else if(arg=="--interactive")a.interactive=true;
        else if(arg=="--record-clean")a.record_clean=true;
        else if(arg=="--allow-core")a.allow_core=true;
        else if(arg=="--windows-minidump")a.windows_minidump=true;
        else throw std::invalid_argument("invalid supervisor option");
    }
    for(;i<argc;++i)a.command.emplace_back(argv[i]);
    if(a.save.empty() || a.command.empty() || a.role.empty() || a.role.size()>40)
        throw std::invalid_argument("missing save/role/command");
    if(!a.build_sha.empty()){
        if(a.build_sha.size()!=40)throw std::invalid_argument("build SHA must be 40 hex digits");
        for(const char c:a.build_sha)
            if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))
                throw std::invalid_argument("build SHA must be hexadecimal");
    }
    #ifdef _WIN32
    if(a.allow_core)throw std::invalid_argument("OS core collection is Linux-only");
    #else
    if(a.windows_minidump)throw std::invalid_argument("Windows minidumps require Windows");
    #endif
    for(const char c:a.role)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'))
            throw std::invalid_argument("invalid host role");
    return a;
}
static std::uint64_t elapsed_ms(Clock::time_point start){
    return static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::milliseconds>(Clock::now()-start).count());
}
#ifndef _WIN32
static volatile std::sig_atomic_t interrupted=0;
static void on_interrupt(int){interrupted=1;}
static void nonblocking(int fd){
    const int old=::fcntl(fd,F_GETFL,0);
    if(old<0 || ::fcntl(fd,F_SETFL,old|O_NONBLOCK)<0)
        throw std::runtime_error("fcntl failed");
}
static bool decimal(std::string_view text,std::uint64_t& result){
    if(text.empty())return false;
    result=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size();
}
static int run(const Args& args){
    int logs[2]{-1,-1},crumbs[2]{-1,-1};
    if(::pipe(logs)!=0 || ::socketpair(AF_UNIX,SOCK_DGRAM,0,crumbs)!=0)
        throw std::runtime_error("cannot create native supervisor pipes");
    nonblocking(logs[0]);nonblocking(crumbs[0]);nonblocking(crumbs[1]);
    const auto started=Clock::now();
    const pid_t supervisor_pid=::getpid();
    const pid_t pid=::fork();
    if(pid<0)throw std::runtime_error("fork failed");
    if(pid==0){
#ifdef __linux__
        // If the supervisor is SIGKILLed, the native game must not silently
        // continue without its authoritative process guardian. A parent-death
        // signal survives execve; checking getppid closes the fork/prctl race.
        if(::prctl(PR_SET_PDEATHSIG,SIGKILL)!=0 || ::getppid()!=supervisor_pid)
            ::_exit(127);
#endif
        (void)::setpgid(0,0);
        ::close(logs[0]);::close(crumbs[0]);
        if(::dup2(logs[1],STDERR_FILENO)<0)::_exit(127);
        if(logs[1]!=STDERR_FILENO)::close(logs[1]);
        const auto descriptor=std::to_string(crumbs[1]);
        (void)::setenv("SGDX_BREADCRUMB_FD",descriptor.c_str(),1);
        struct rlimit limit{};
        limit.rlim_cur=args.allow_core?64U*1024U*1024U:0;
        limit.rlim_max=args.allow_core?64U*1024U*1024U:0;
        (void)::setrlimit(RLIMIT_CORE,&limit);
        std::vector<char*> params;
        for(const auto& value:args.command)params.push_back(const_cast<char*>(value.c_str()));
        params.push_back(nullptr);
        ::execvp(params.front(),params.data());
        std::cerr<<"SGDX-02 exec failed\n";
        ::_exit(127);
    }
    (void)::setpgid(pid,pid);
    ::close(logs[1]);::close(crumbs[1]);
    std::signal(SIGINT,on_interrupt);std::signal(SIGTERM,on_interrupt);
    dx::ProcessIncident incident;
    incident.pid=pid;incident.role=args.role;incident.build_sha=args.build_sha;
    incident.core_opt_in=args.allow_core;
    bool timed_out=false,cancelled=false,reaped=false,log_open=true,crumb_open=true;
    int status=0;
    std::string pending;
    const auto read_pipe=[&](int fd,bool& open,bool is_crumb){
        char buffer[4096];
        for(;;){
            const auto n=::read(fd,buffer,sizeof(buffer));
            if(n>0){
                if(!is_crumb){
                    incident.stderr_tail.append(buffer,static_cast<std::size_t>(n));
                    if(incident.stderr_tail.size()>8192)
                        incident.stderr_tail.erase(0,incident.stderr_tail.size()-8192);
                }else{
                    pending.append(buffer,static_cast<std::size_t>(n));
                    if(pending.size()>1024)pending.clear();
                    for(std::size_t end;(end=pending.find('\n'))!=std::string::npos;){
                        const std::string line=pending.substr(0,end);
                        pending.erase(0,end+1);
                        if(line.rfind("COMMIT|",0)!=0)continue;
                        const auto split=line.find('|',7);
                        if(split==std::string::npos)continue;
                        std::uint64_t tick=0,generation=0;
                        if(!decimal(std::string_view(line).substr(7,split-7),tick) ||
                           !decimal(std::string_view(line).substr(split+1),generation))continue;
                        if(tick<incident.last_committed_tick ||
                           generation<incident.last_world_generation)continue;
                        incident.last_committed_tick=tick;
                        incident.last_world_generation=generation;
                        ++incident.breadcrumb_count;
                    }
                }
                continue;
            }
            if(n==0){open=false;::close(fd);}
            else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){
                open=false;::close(fd);
            }
            break;
        }
    };
    auto reaped_at=Clock::now();
    while(!reaped || log_open || crumb_open){
        if(!reaped){
            const pid_t done=::waitpid(pid,&status,WNOHANG);
            if(done==pid || (done<0 && errno==ECHILD)){
                reaped=true;reaped_at=Clock::now();
                // Descendants cannot hold the parent's pipes open indefinitely.
                (void)::kill(-pid,SIGKILL);
            }else if(done<0 && errno!=EINTR)throw std::runtime_error("waitpid failed");
        }
        if(!reaped && ((!args.interactive && elapsed_ms(started)>=static_cast<std::uint64_t>(args.timeout_ms))
                       || interrupted)){
            timed_out=!interrupted;cancelled=interrupted;
            (void)::kill(-pid,SIGKILL);(void)::kill(pid,SIGKILL);
        }
        struct pollfd fds[2]{};int nfds=0;
        if(log_open)fds[nfds++]={logs[0],POLLIN|POLLHUP,0};
        if(crumb_open)fds[nfds++]={crumbs[0],POLLIN|POLLHUP,0};
        if(nfds>0)(void)::poll(fds,nfds,40);
        if(log_open)read_pipe(logs[0],log_open,false);
        if(crumb_open)read_pipe(crumbs[0],crumb_open,true);
        if(reaped && std::chrono::duration_cast<std::chrono::milliseconds>(
           Clock::now()-reaped_at).count()>300)break;
    }
    if(log_open)::close(logs[0]);
    if(crumb_open)::close(crumbs[0]);
    incident.elapsed_ms=elapsed_ms(started);
    int return_code=1;
    if(cancelled){incident.reason="cancelled";return_code=130;}
    else if(timed_out){incident.reason="timeout";return_code=124;}
    else if(WIFSIGNALED(status)){
        incident.reason="signal";incident.signal_number=WTERMSIG(status);
        return_code=128+incident.signal_number;
    }else if(WIFEXITED(status)){
        incident.exit_code=WEXITSTATUS(status);
        incident.reason=incident.exit_code==0?"clean_exit":"nonzero_exit";
        return_code=incident.exit_code;
    }else incident.reason="unknown_exit";
    if(return_code!=0 || args.record_clean){
        if(!dx::record_process_incident(args.save,incident))
            std::cerr<<"SGDX-02 diagnostic sidecar write failed\n";
    }
    return return_code;
}
#else
// Windows Job Objects make the server and its descendants one supervised unit.
// Native minidumps require a separate crash-time debugger/WER integration;
// writing a dump after process exit would not preserve crash-time memory.
static std::wstring widen(const std::string& input){
    if(input.size()>static_cast<std::size_t>(INT_MAX))throw std::invalid_argument("argument too long");
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        input.data(),static_cast<int>(input.size()),nullptr,0);
    if(length<=0)throw std::invalid_argument("invalid UTF-8 argument");
    std::wstring out(static_cast<std::size_t>(length),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        input.data(),static_cast<int>(input.size()),out.data(),length);
    return out;
}
static std::wstring quote(const std::wstring& value){
    std::wstring result=L"\"";
    std::size_t slash=0;
    for(wchar_t ch:value){
        if(ch==L'\\'){++slash;continue;}
        if(ch==L'\"'){result.append(slash*2+1,L'\\');slash=0;result+=ch;}
        else {result.append(slash,L'\\');slash=0;result+=ch;}
    }
    result.append(slash*2,L'\\');return result+L'\"';
}
static int run(const Args& args){
    // A failure to contain a child must fail closed (no unsupervised process).
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    if(!job)throw std::runtime_error("CreateJobObject failed");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){
        CloseHandle(job);throw std::runtime_error("Job Object setup failed");
    }
    std::wstring command_line;
    for(const auto& token:args.command){
        if(!command_line.empty())command_line+=L' ';
        command_line+=quote(widen(token));
    }
    const auto executable=widen(args.command.front());
    // Preserve the game's original stdout/stdin/stderr stream handles.
    // Never write SGDX data to the player's SGVIEW stdout channel.
    const HANDLE source_in=GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE source_out=GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE source_err=GetStdHandle(STD_ERROR_HANDLE);
    HANDLE child_in=nullptr,child_out=nullptr,child_err=nullptr;
    const auto duplicate=[&](HANDLE source,HANDLE& dest){
        return source && source!=INVALID_HANDLE_VALUE &&
          DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&dest,
                          0,TRUE,DUPLICATE_SAME_ACCESS);
    };
    if(!duplicate(source_in,child_in) || !duplicate(source_out,child_out) ||
       !duplicate(source_err,child_err)){
        if(child_in)CloseHandle(child_in);
        if(child_out)CloseHandle(child_out);
        if(child_err)CloseHandle(child_err);
        CloseHandle(job);
        throw std::runtime_error("cannot inherit native game stream handles");
    }
    SIZE_T attributes_bytes=0;
    (void)InitializeProcThreadAttributeList(nullptr,1,0,&attributes_bytes);
    if(!attributes_bytes){
        CloseHandle(child_in);CloseHandle(child_out);CloseHandle(child_err);
        CloseHandle(job);
        throw std::runtime_error("native handle-list sizing failed");
    }
    std::vector<unsigned char> attribute_storage(attributes_bytes);
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);
    si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput=child_in;
    si.StartupInfo.hStdOutput=child_out;
    si.StartupInfo.hStdError=child_err;
    si.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        attribute_storage.data());
    if(!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&attributes_bytes)){
        CloseHandle(child_in);CloseHandle(child_out);CloseHandle(child_err);
        CloseHandle(job);
        throw std::runtime_error("native handle-list setup failed");
    }
    HANDLE allowed[3]={child_in,child_out,child_err};
    const bool restricted=UpdateProcThreadAttribute(si.lpAttributeList,0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(allowed),
        nullptr,nullptr)!=FALSE;
    PROCESS_INFORMATION pi{};
    const auto started=Clock::now();
    const BOOL launched=restricted && CreateProcessW(
        executable.c_str(),command_line.data(),nullptr,nullptr,TRUE,
        CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT|
          (args.windows_minidump?DEBUG_ONLY_THIS_PROCESS:0),
        nullptr,nullptr,&si.StartupInfo,&pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    CloseHandle(child_in);CloseHandle(child_out);CloseHandle(child_err);
    if(!launched){CloseHandle(job);throw std::runtime_error("CreateProcess failed");}
    if(!AssignProcessToJobObject(job,pi.hProcess)){
        TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        CloseHandle(job);throw std::runtime_error("Job Object assignment failed");
    }
    if(ResumeThread(pi.hThread)==static_cast<DWORD>(-1)){
        TerminateJobObject(job,1);
        WaitForSingleObject(pi.hProcess,INFINITE);
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);
        throw std::runtime_error("could not resume supervised game");
    }
    bool timed_out=false,crashed=false,dump_written=false;
    std::wstring dump_name;
    std::string dump_failure;
    DWORD outcome=WAIT_OBJECT_0;
    if(args.windows_minidump){
        // Debug the DIRECT game process only. Its descendants remain in the
        // kill-on-close Job Object, not a substitute for game authorization.
        // A second-chance EXCEPTION_DEBUG_EVENT occurs BEFORE the OS kills
        // an unhandled crashing process. Capture it while the thread is held.
        bool exited=false;
        bool initial_debug_breakpoint=false;
        auto stop_at=started+std::chrono::milliseconds(args.timeout_ms);
        while(!exited){
            DEBUG_EVENT event{};
            if(WaitForDebugEvent(&event,50)){
                DWORD reply=DBG_CONTINUE;
                if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT &&
                   event.u.CreateProcessInfo.hFile)
                    CloseHandle(event.u.CreateProcessInfo.hFile);
                if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT &&
                   event.u.LoadDll.hFile)
                    CloseHandle(event.u.LoadDll.hFile);
                if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){
                    const auto code=event.u.Exception.ExceptionRecord.ExceptionCode;
                    if(event.u.Exception.dwFirstChance){
                        // Never swallow real C++/SEH errors. The Windows loader
                        // uses an initial breakpoint to initialize a debugger.
                        if(code==EXCEPTION_BREAKPOINT && !initial_debug_breakpoint){
                            initial_debug_breakpoint=true;
                            reply=DBG_CONTINUE;
                        }else reply=DBG_EXCEPTION_NOT_HANDLED;
                    }else {
                        crashed=true;reply=DBG_EXCEPTION_NOT_HANDLED;
                        if(!dump_written)
                            dump_written=dx::windows_dump::capture_second_chance(
                                args.save,pi,event,dump_name,&dump_failure);
                    }
                }
                if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)
                    exited=true;
                if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,reply)){
                    TerminateJobObject(job,1);
                    break;
                }
            }else if(GetLastError()!=ERROR_SEM_TIMEOUT){
                TerminateJobObject(job,1);
                break;
            }
            if(!args.interactive && !timed_out && Clock::now()>=stop_at && !exited){
                timed_out=true;outcome=WAIT_TIMEOUT;
                TerminateJobObject(job,124);
                stop_at=Clock::now()+std::chrono::seconds(3);
            }
            if(timed_out && Clock::now()>=stop_at)break;
        }
    }else{
        outcome=WaitForSingleObject(pi.hProcess,args.interactive?INFINITE:static_cast<DWORD>(args.timeout_ms));
        if(outcome==WAIT_TIMEOUT){timed_out=true;TerminateJobObject(job,124);}
        if(outcome!=WAIT_OBJECT_0 && outcome!=WAIT_TIMEOUT)
            TerminateJobObject(job,1);
    }
    WaitForSingleObject(pi.hProcess,INFINITE);
    DWORD status=1;GetExitCodeProcess(pi.hProcess,&status);
    dx::ProcessIncident incident;
    incident.role=args.role;incident.build_sha=args.build_sha;
    incident.pid=static_cast<std::int64_t>(pi.dwProcessId);
    incident.elapsed_ms=elapsed_ms(started);
    incident.exit_code=static_cast<int>(status);
    incident.reason=timed_out?"timeout":crashed?"unhandled_exception":
                  status==0?"clean_exit":"nonzero_exit";
    if(args.windows_minidump)
        incident.detail=crashed?
            (dump_written?"crash-time minidump recorded":
              "crash-time minidump unavailable"+
              (dump_failure.empty()?std::string{}:" ["+dump_failure+"]")):
            "no second-chance exception observed";
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);
    if(incident.exit_code!=0 || args.record_clean)
        if(!dx::record_process_incident(args.save,incident))
            std::cerr<<"SGDX-02 diagnostic sidecar write failed\n";
    return outcome==WAIT_TIMEOUT?124:static_cast<int>(status);
}
#endif
#ifdef _WIN32
// Windows CRT narrow argv uses the machine's active ANSI code page, not
// necessarily UTF-8. Godot's normal project/user-data folder contains an
// em dash ("Single-Galaxy — Graybox FPS"). Its narrow argv becomes CP1252
// byte 0x97 on many physical Windows systems. Reinterpreting that as UTF-8
// in widen() fails closed before the first original station PERSON frame.
// Use genuine Unicode process startup, then convert ONCE to validated UTF-8.
// Existing parse/authorization semantics are identical on both platforms.
static std::string utf8_argument(const wchar_t* argument){
    if(!argument)throw std::invalid_argument("null Windows argument");
    const int needed=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argument,-1,
                                        nullptr,0,nullptr,nullptr);
    if(needed<=0)throw std::invalid_argument("invalid Windows Unicode command argument");
    std::string encoded(static_cast<std::size_t>(needed),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argument,-1,
                           encoded.data(),needed,nullptr,nullptr)!=needed)
        throw std::invalid_argument("Windows command line UTF-8 conversion failed");
    encoded.pop_back(); // exclude terminating NUL
    return encoded;
}
int wmain(int argc,wchar_t** argv){
    try{
        std::vector<std::string> utf8;
        utf8.reserve(static_cast<std::size_t>(argc));
        for(int i=0;i<argc;++i)utf8.push_back(utf8_argument(argv[i]));
        std::vector<char*> pointers;
        pointers.reserve(utf8.size());
        for(auto& arg:utf8)pointers.push_back(arg.data());
        return run(parse(argc,pointers.data()));
    }catch(const std::exception& e){
        std::cerr<<"SGDX-02 supervisor configuration failure: "<<e.what()<<'\n';
        return 64;
    }
}
#else
int main(int argc,char** argv){
    try{return run(parse(argc,argv));}
    catch(const std::exception& e){
        std::cerr<<"SGDX-02 supervisor configuration failure: "<<e.what()<<'\n';
        return 64;
    }
}
#endif
