#pragma once
// SGDX-02: independent host/worker process exit evidence. No game DB writes.
#include "galaxy/ServerDiagnostics.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
namespace galaxy::server_dx {
struct ProcessIncident {
    std::string role;
    std::string build_sha;
    std::string reason;
    std::int64_t pid=0;
    int exit_code=-1;
    int signal_number=0;
    std::uint64_t elapsed_ms=0;
    std::uint64_t last_committed_tick=0;
    std::uint64_t last_world_generation=0;
    std::uint64_t breadcrumb_count=0;
    bool core_opt_in=false;
    std::string stderr_tail;
};
// Stderr is arbitrary bytes; emit ASCII-safe JSON even for invalid UTF-8.
// This preserves a byte-level representation without invoking a text decoder.
inline std::string json_stderr_bytes(std::string_view input,
                                     std::size_t max_bytes=8192) {
    constexpr char hex[]="0123456789abcdef";
    std::string out="\"";
    const auto size=std::min(input.size(),max_bytes);
    for(std::size_t i=0;i<size;++i){
        const unsigned char c=static_cast<unsigned char>(input[i]);
        if(c=='"' || c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<0x20 || c>=0x7f) {
            out+="\\u00";out+=hex[c>>4];out+=hex[c&15];
        } else out+=static_cast<char>(c);
    }
    return out+'\"';
}
inline bool record_process_incident(const std::filesystem::path& save,
                                    const ProcessIncident& incident) noexcept {
    namespace fs=std::filesystem;
    try {
        if(save.empty())return false;
        fs::path dir=save;dir+=".diagnostics";
        std::error_code ec;
        if(fs::is_symlink(dir,ec))return false;
        fs::create_directories(dir,ec);
        if(ec)return false;
        fs::permissions(dir,fs::perms::owner_all,fs::perm_options::replace,ec);
        if(ec)return false;
        const auto epoch_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        const auto name="process-"+std::to_string(epoch_ms)+"-"+
                        std::to_string(stamp)+".json";
        const fs::path final=dir/name;
        const fs::path partial=dir/(name+".tmp");
        std::ofstream file(partial,std::ios::binary|std::ios::trunc);
        if(!file)return false;
        fs::permissions(partial,fs::perms::owner_read|fs::perms::owner_write,
                        fs::perm_options::replace,ec);
        if(ec){file.close();fs::remove(partial,ec);return false;}
        file<<"{\"schema\":\"SGDX-02\",\"kind\":\"process_exit\""
            <<",\"epoch_ms\":"<<epoch_ms
            <<",\"role\":"<<json_string(incident.role,80)
            <<",\"build_sha\":"<<json_string(incident.build_sha,40)
            <<",\"reason\":"<<json_string(incident.reason,48)
            <<",\"pid\":"<<incident.pid
            <<",\"exit_code\":"<<incident.exit_code
            <<",\"signal\":"<<incident.signal_number
            <<",\"elapsed_ms\":"<<incident.elapsed_ms
            <<",\"last_committed_tick\":"<<incident.last_committed_tick
            <<",\"last_world_generation\":"<<incident.last_world_generation
            <<",\"breadcrumb_count\":"<<incident.breadcrumb_count
            <<",\"core_opt_in\":"<<(incident.core_opt_in?"true":"false")
            <<",\"stderr_tail\":"<<json_stderr_bytes(incident.stderr_tail,8192)<<"}\n";
        file.flush();
        const bool success=file.good();file.close();
        if(!success || !file.good()){fs::remove(partial,ec);return false;}
        fs::rename(partial,final,ec);
        if(ec){fs::remove(partial,ec);return false;}
        // Keep crash loops bounded, independent of SGDX-01 exception records.
        std::vector<fs::path> files;
        for(const auto& item:fs::directory_iterator(dir)) {
            if(item.is_regular_file() &&
               item.path().filename().string().rfind("process-",0)==0 &&
               item.path().extension()==".json")files.push_back(item.path());
        }
        std::sort(files.begin(),files.end());
        for(std::size_t i=0;files.size()>16 && i<files.size()-16;++i)
            fs::remove(files[i],ec);
        return true;
    }catch(...){return false;}
}
} // namespace galaxy::server_dx
