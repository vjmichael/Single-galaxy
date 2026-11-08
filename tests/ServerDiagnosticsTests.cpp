#include "galaxy/ServerDiagnostics.hpp"
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("SGDX check failed: ")+#x);}while(false)
int main(){
    const auto save=fs::temp_directory_path()/
      ("sgdx_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    fs::path dir=save;dir+=".diagnostics";
    const auto cleanup=[&]{std::error_code ec;fs::remove_all(dir,ec);};
    try {
        const galaxy::server_dx::Progress progress{27,29,"world_step"};
        CHECK(galaxy::server_dx::record_exception(save,"colony_host",
            "sqlite failed: \"database\"\nnext\\row",progress));
        std::vector<fs::path> files;
        for(const auto& e:fs::directory_iterator(dir))files.push_back(e.path());
        CHECK(files.size()==1);
        std::ifstream in(files[0]);
        const std::string result((std::istreambuf_iterator<char>(in)),{});
        CHECK(result.find("\"last_committed_tick\":27")!=std::string::npos);
        CHECK(result.find("\"last_world_generation\":29")!=std::string::npos);
        CHECK(result.find("\\u000anext\\\\row")!=std::string::npos);
        CHECK(result.find("\"schema\":\"SGDX-01\"")!=std::string::npos);
        CHECK(result.find(save.string())==std::string::npos);
#ifndef _WIN32
        const auto mask=fs::status(dir).permissions();
        CHECK((mask&fs::perms::group_all)==fs::perms::none);
        CHECK((mask&fs::perms::others_all)==fs::perms::none);
        const auto fmask=fs::status(files[0]).permissions();
        CHECK((fmask&fs::perms::group_all)==fs::perms::none);
        CHECK((fmask&fs::perms::others_all)==fs::perms::none);
#endif
        for(int i=0;i<21;++i)
            CHECK(galaxy::server_dx::record_exception(save,"colony_host",
                "failure "+std::to_string(i),progress));
        std::size_t count=0;
        for(const auto& e:fs::directory_iterator(dir)){
            CHECK(e.path().extension()==".json");++count;
        }
        CHECK(count==16);
        cleanup();
        const auto symlink=fs::temp_directory_path()/
            ("sgdx_link_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#ifndef _WIN32
        fs::create_directory(symlink);
        fs::create_directory_symlink(symlink,dir);
        CHECK(!galaxy::server_dx::record_exception(save,"colony_host","blocked",progress));
        fs::remove(dir);fs::remove(symlink);
#endif
        std::cout<<"PASS SGDX-01 private incidents, escaping, context, 16-record retention and symlink refusal\n";
        return 0;
    }catch(...) {cleanup();throw;}
}
