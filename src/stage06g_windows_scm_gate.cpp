// S6-GK/WIN: physical Windows SCM-owned, nested-process Job-Object custody
// acceptance. This standalone executable is NOT an installer or game service.
// It launches ONLY the compiled fixed SGDX fault-child "descendant" fixture.
// The operator must separately create the fixed-name SCM service explicitly.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <exception>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr wchar_t SERVICE_NAME[]=L"SingleGalaxyS6GKernelGate";
std::wstring child_path,output_dir;
std::string source_sha;
SERVICE_STATUS_HANDLE service_handle=nullptr;
SERVICE_STATUS service_state{};
HANDLE stop_event=nullptr;

void status(DWORD state,DWORD error=NO_ERROR){
    service_state.dwServiceType=SERVICE_WIN32_OWN_PROCESS;
    service_state.dwCurrentState=state;
    service_state.dwControlsAccepted=state==SERVICE_RUNNING?
        SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;
    service_state.dwWin32ExitCode=error;
    service_state.dwWaitHint=state==SERVICE_STOP_PENDING?10000:0;
    if(service_handle)SetServiceStatus(service_handle,&service_state);
}
DWORD WINAPI control(DWORD event,DWORD,LPVOID,LPVOID){
    if(event==SERVICE_CONTROL_STOP || event==SERVICE_CONTROL_SHUTDOWN){
        status(SERVICE_STOP_PENDING);
        if(stop_event)SetEvent(stop_event);
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}
std::string json_of(const std::string& stage,DWORD pid,const std::vector<DWORD>& children,
                    bool terminated,bool success){
    std::string out="{\n  \"schema\": \"SG-S6G-WINDOWS-SCM-JOB-01\",\n";
    out+="  \"source_sha\": \""+source_sha+"\",\n";
    out+="  \"stage\": \""+stage+"\",\n";
    out+="  \"scm_service\": \"SingleGalaxyS6GKernelGate\",\n";
    out+="  \"service_pid\": "+std::to_string(GetCurrentProcessId())+",\n";
    out+="  \"supervised_pid\": "+std::to_string(pid)+",\n";
    out+="  \"job_process_ids\": [";
    for(std::size_t i=0;i<children.size();++i){
        if(i)out+=",";
        out+=std::to_string(children[i]);
    }
    out+="],\n  \"job_kill_on_close\": true,\n";
    out+=std::string("  \"verified_terminated\": ")+(terminated?"true":"false")+",\n";
    out+=std::string("  \"passed\": ")+(success?"true":"false")+"\n}\n";
    return out;
}
void write_receipt(const std::wstring& name,const std::string& contents){
    const auto dir=std::filesystem::path(output_dir);
    if(!std::filesystem::is_directory(dir) || std::filesystem::is_symlink(dir))
        throw std::runtime_error("SCM receipt root invalid");
    const auto temp=dir/(name+L".tmp");
    const auto target=dir/(name+L".json");
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);
     if(!f)throw std::runtime_error("SCM receipt cannot open");
     f<<contents;f.flush();if(!f)throw std::runtime_error("SCM receipt cannot write");}
    if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("SCM receipt atomic publish failed");
}
std::vector<DWORD> query_job(HANDLE job){
    alignas(JOBOBJECT_BASIC_PROCESS_ID_LIST) unsigned char bytes[
        sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST)+sizeof(ULONG_PTR)*16]{};
    auto* info=reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(bytes);
    if(!QueryInformationJobObject(job,JobObjectBasicProcessIdList,info,
                                  sizeof(bytes),nullptr))
        throw std::runtime_error("SCM JobObjectBasicProcessIdList failed");
    if(info->NumberOfAssignedProcesses!=info->NumberOfProcessIdsInList ||
       info->NumberOfAssignedProcesses<2 || info->NumberOfAssignedProcesses>16)
        return {};
    std::vector<DWORD> result;
    for(DWORD i=0;i<info->NumberOfProcessIdsInList;++i)
        result.push_back(static_cast<DWORD>(info->ProcessIdList[i]));
    return result;
}
void custody(){
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    if(!job)throw std::runtime_error("SCM CreateJobObject failed");
    PROCESS_INFORMATION pi{};
    std::vector<HANDLE> observed;
    try{
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
            throw std::runtime_error("SCM kernel kill-on-close configuration failed");
        auto line=L"\""+child_path+L"\" descendant";
        std::vector<wchar_t> mutable_line(line.begin(),line.end());
        mutable_line.push_back(L'\0');
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        if(!CreateProcessW(child_path.c_str(),mutable_line.data(),nullptr,nullptr,FALSE,
             CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi))
            throw std::runtime_error("SCM fixture CreateProcess failed");
        if(!AssignProcessToJobObject(job,pi.hProcess))
            throw std::runtime_error("SCM fixture not assigned to kernel Job");
        if(ResumeThread(pi.hThread)==static_cast<DWORD>(-1))
            throw std::runtime_error("SCM fixture ResumeThread failed");
        std::vector<DWORD> ids;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(12);
        while(std::chrono::steady_clock::now()<until && WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){
            ids=query_job(job);
            if(ids.size()>=2)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        if(ids.size()<2 || std::find(ids.begin(),ids.end(),pi.dwProcessId)==ids.end())
            throw std::runtime_error("SCM did not observe native descendant inside Job");
        for(DWORD pid:ids){
            auto h=OpenProcess(SYNCHRONIZE,FALSE,pid);
            if(!h)throw std::runtime_error("SCM could not independently observe Job PID");
            observed.push_back(h);
        }
        write_receipt(L"job-start",json_of("running",pi.dwProcessId,ids,false,true));
        status(SERVICE_RUNNING);
        HANDLE watched[]={stop_event,pi.hProcess};
        const auto outcome=WaitForMultipleObjects(2,watched,FALSE,INFINITE);
        status(SERVICE_STOP_PENDING);
        if(outcome!=WAIT_OBJECT_0)
            throw std::runtime_error("SCM fixture exited before explicit service stop");
        // The kernel, not TerminateProcess(child), must terminate the entire
        // job including its detached native child. Save live PID handles first.
        CloseHandle(job);job=nullptr;
        bool all_dead=true;
        for(HANDLE h:observed)
            if(WaitForSingleObject(h,5000)!=WAIT_OBJECT_0)all_dead=false;
        write_receipt(L"job-stop",json_of("stopped",pi.dwProcessId,ids,all_dead,all_dead));
        if(!all_dead)throw std::runtime_error("SCM Job left a descendant active");
    }catch(...){
        if(job)TerminateJobObject(job,1);
        if(job)CloseHandle(job);
        for(auto h:observed)CloseHandle(h);
        if(pi.hThread)CloseHandle(pi.hThread);
        if(pi.hProcess)CloseHandle(pi.hProcess);
        throw;
    }
    for(auto h:observed)CloseHandle(h);
    if(pi.hThread)CloseHandle(pi.hThread);
    if(pi.hProcess)CloseHandle(pi.hProcess);
}
void WINAPI service_main(DWORD,LPWSTR*){
    service_handle=RegisterServiceCtrlHandlerExW(SERVICE_NAME,control,nullptr);
    if(!service_handle)return;
    status(SERVICE_START_PENDING);
    stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stop_event){status(SERVICE_STOPPED,ERROR_NOT_ENOUGH_MEMORY);return;}
    try{custody();status(SERVICE_STOPPED);}
    catch(const std::exception& e){
        try{write_receipt(L"job-failure",json_of(e.what(),0,{},false,false));}
        catch(...){}
        status(SERVICE_STOPPED,ERROR_GEN_FAILURE);
    }
    CloseHandle(stop_event);stop_event=nullptr;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=8 || std::wstring(argv[1])!=L"--service" ||
       std::wstring(argv[2])!=L"--fault-child" ||
       std::wstring(argv[4])!=L"--evidence" ||
       std::wstring(argv[6])!=L"--sha")return 64;
    child_path=argv[3];output_dir=argv[5];
    const std::wstring sha=argv[7];
    if(sha.size()!=40 || !std::all_of(sha.begin(),sha.end(),[](wchar_t c){
       return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))return 64;
    source_sha.assign(sha.begin(),sha.end());
    if(!std::filesystem::path(child_path).is_absolute() ||
       !std::filesystem::is_regular_file(child_path) ||
       !std::filesystem::path(output_dir).is_absolute())return 64;
    SERVICE_TABLE_ENTRYW table[]={
        {const_cast<LPWSTR>(SERVICE_NAME),service_main},{nullptr,nullptr}};
    return StartServiceCtrlDispatcherW(table)?0:static_cast<int>(GetLastError());
}
#endif
