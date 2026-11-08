#include "galaxy/ServerDiagnostics.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
namespace fs=std::filesystem;
int main(){
    const auto save=fs::temp_directory_path()/
      ("sgdx_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    fs::path dir=save;dir+=".diagnostics";
    const auto cleanup=[&]{std::error_code ec;fs::remove_all(dir,ec);};
    try {
        const galaxy::server_dx::Progress progress{27,29,"world_step"};
        assert(galaxy::server_dx::record_exception(save,"colony_host",
            "sqlite failed: \"database\"\nnext\\row",progress));
        std::vector<fs::path> files;
        for(const auto& e:fs::directory_iterator(dir))files.push_back(e.path());
        assert(files.size()==1);
        std::ifstream in(files[0]);
        const std::string result((std::istreambuf_iterator<char>(in)),{});
        assert(result.find("\"last_committed_tick\":27")!=std::string::npos);
        assert(result.find("\"last_world_generation\":29")!=std::string::npos);
        assert(result.find("\\u000anext\\\\row")!=std::string::npos);
        assert(result.find("\"schema\":\"SGDX-01\"")!=std::string::npos);
        assert(result.find(save.string())==std::string::npos);
#ifndef _WIN32
        const auto mask=fs::status(dir).permissions();
        assert((mask&fs::perms::group_all)==fs::perms::none);
        assert((mask&fs::perms::others_all)==fs::perms::none);
        const auto fmask=fs::status(files[0]).permissions();
        assert((fmask&fs::perms::group_all)==fs::perms::none);
        assert((fmask&fs::perms::others_all)==fs::perms::none);
#endif
        for(int i=0;i<21;++i)
            assert(galaxy::server_dx::record_exception(save,"colony_host",
                "failure "+std::to_string(i),progress));
        std::size_t count=0;
        for(const auto& e:fs::directory_iterator(dir)){
            assert(e.path().extension()==".json");++count;
        }
        assert(count==16);
        cleanup();
        const auto symlink=fs::temp_directory_path()/
            ("sgdx_link_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#ifndef _WIN32
        fs::create_directory(symlink);
        fs::create_directory_symlink(symlink,dir);
        assert(!galaxy::server_dx::record_exception(save,"colony_host","blocked",progress));
        fs::remove(dir);fs::remove(symlink);
#endif
        std::cout<<"PASS SGDX-01 private incidents, escaping, context, 16-record retention and symlink refusal\n";
        return 0;
    }catch(...) {cleanup();throw;}
}
