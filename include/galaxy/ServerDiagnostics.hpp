#pragma once
// SGDX-01: local-only failure evidence for the native single-writer game host.
// This is an exception incident report, not an OS crash dump or a replay.
// Never route these records through SGVIEW, player SSE, or public telemetry.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::server_dx {
struct Progress {
    std::uint64_t committed_tick=0;
    std::uint64_t world_generation=0;
    const char* phase="startup";
};
inline std::string json_string(std::string_view input,std::size_t limit=512) {
    constexpr char hex[]="0123456789abcdef";
    std::string out="\"";
    const auto size=std::min(input.size(),limit);
    for(std::size_t i=0;i<size;++i) {
        const unsigned char c=static_cast<unsigned char>(input[i]);
        if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<0x20) {
            out+="\\u00";out+=hex[c>>4];out+=hex[c&15];
        } else out+=static_cast<char>(c);
    }
    if(input.size()>limit)out+=" [truncated]";
    return out+'"';
}
inline bool record_exception(const std::filesystem::path& save_path,
                             std::string_view host_role,
                             std::string_view exception_message,
                             const Progress& context) noexcept {
    namespace fs=std::filesystem;
    try {
        if(save_path.empty())return false;
        fs::path dir=save_path;
        dir+=".diagnostics";
        std::error_code ec;
        if(fs::is_symlink(dir,ec))return false;
        fs::create_directories(dir,ec);
        if(ec)return false;
        fs::permissions(dir,fs::perms::owner_all,
                        fs::perm_options::replace,ec);
        if(ec)return false;
        const auto epoch_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        const auto name="incident-"+std::to_string(epoch_ms)+"-"+
                        std::to_string(stamp)+".json";
        const fs::path final=dir/name;
        const fs::path partial=dir/(name+".tmp");
        std::ofstream file(partial,std::ios::binary|std::ios::trunc);
        if(!file)return false;
        fs::permissions(partial,fs::perms::owner_read|fs::perms::owner_write,
                        fs::perm_options::replace,ec);
        if(ec){file.close();fs::remove(partial,ec);return false;}
        file<<"{\"schema\":\"SGDX-01\",\"kind\":\"unhandled_exception\","
               "\"epoch_ms\":"<<epoch_ms
            <<",\"host\":"<<json_string(host_role,80)
            <<",\"phase\":"<<json_string(context.phase?context.phase:"unknown",80)
            <<",\"last_committed_tick\":"<<context.committed_tick
            <<",\"last_world_generation\":"<<context.world_generation
            <<",\"message\":"<<json_string(exception_message)<<"}\n";
        file.flush();
        const bool written=file.good();
        file.close();
        if(!written || !file.good()){
            fs::remove(partial,ec);return false;
        }
        fs::rename(partial,final,ec);
        if(ec){fs::remove(partial,ec);return false;}
        // A crash loop must not fill the disk with unbounded incident records.
        std::vector<fs::path> records;
        for(const auto& e:fs::directory_iterator(dir)) {
            if(e.is_regular_file() &&
               e.path().filename().string().rfind("incident-",0)==0 &&
               e.path().extension()==".json")records.push_back(e.path());
        }
        std::sort(records.begin(),records.end());
        if(records.size()>16)for(std::size_t i=0;i<records.size()-16;++i){
            fs::remove(records[i],ec);
        }
        return true;
    }catch(...) {return false;} // diagnostics must never hide the real crash
}
} // namespace galaxy::server_dx
