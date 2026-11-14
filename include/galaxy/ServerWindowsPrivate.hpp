#pragma once
// Private NTFS diagnostic paths: current user and SYSTEM only.
// Never follow a pre-existing reparse-point diagnostics directory.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <string>
#include <vector>
namespace galaxy::server_dx::windows_private {
struct ScopedSecurity {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    ~ScopedSecurity(){if(descriptor)LocalFree(descriptor);}
    bool initialize(){
        HANDLE token=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
        DWORD bytes=0;
        (void)GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
        std::vector<unsigned char> user(bytes);
        const bool ok=bytes>0 && GetTokenInformation(token,TokenUser,
            user.data(),bytes,&bytes);
        CloseHandle(token);
        if(!ok)return false;
        LPWSTR sid=nullptr;
        if(!ConvertSidToStringSidW(
            reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid))return false;
        const std::wstring sddl=L"D:P(A;;FA;;;SY)(A;;FA;;;" +
                                 std::wstring(sid)+L")";
        LocalFree(sid);
        return ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=FALSE;
    }
    SECURITY_ATTRIBUTES attributes()const {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength=sizeof(sa);
        sa.lpSecurityDescriptor=descriptor;
        sa.bInheritHandle=FALSE;
        return sa;
    }
    bool protect_directory(const std::wstring& directory)const{
        DWORD attr=GetFileAttributesW(directory.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES){
            auto sa=attributes();
            if(!CreateDirectoryW(directory.c_str(),&sa) &&
               GetLastError()!=ERROR_ALREADY_EXISTS)return false;
            attr=GetFileAttributesW(directory.c_str());
        }
        if(attr==INVALID_FILE_ATTRIBUTES ||
           !(attr&FILE_ATTRIBUTE_DIRECTORY) ||
           (attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        BOOL present=FALSE,defaulted=FALSE;
        PACL dacl=nullptr;
        if(!GetSecurityDescriptorDacl(descriptor,&present,&dacl,&defaulted) ||
           !present || !dacl)return false;
        return SetNamedSecurityInfoW(const_cast<LPWSTR>(directory.c_str()),
            SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|
            PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr,nullptr,dacl,nullptr)==ERROR_SUCCESS;
    }
};
} // namespace galaxy::server_dx::windows_private
#endif
