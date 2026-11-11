#pragma once
// FP3D-WIN-UTF8: Windows console hosts MUST receive Unicode argv as UTF-16
// and convert once to validated UTF-8. The Godot project/user-data folder
// contains non-ASCII characters (e.g. "Single-Galaxy — Graybox FPS").
// CRT narrow main() argv is an ANSI-codepage representation, NOT guaranteed
// UTF-8; passing it to sqlite3_open_v2() can produce SQLITE_CANTOPEN while
// an ANSI-decoded filesystem lease appears to succeed.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::windows_utf8_argv {
inline std::string convert(const wchar_t* wide) {
    if (!wide) throw std::invalid_argument("null Windows argument");
    const int required=WideCharToMultiByte(
        CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,nullptr,0,nullptr,nullptr);
    if(required<=0)throw std::invalid_argument("invalid UTF-16 Windows argument");
    std::string utf8(static_cast<std::size_t>(required),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,
                           utf8.data(),required,nullptr,nullptr)!=required)
        throw std::invalid_argument("Windows UTF-8 argument conversion failed");
    utf8.pop_back(); // remove only terminating NUL
    return utf8;
}
inline std::vector<std::string> convert_all(int argc,wchar_t** argv) {
    if(argc<1||!argv)throw std::invalid_argument("empty Windows arguments");
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(argc));
    for(int i=0;i<argc;++i)result.push_back(convert(argv[i]));
    return result;
}
inline std::vector<char*> views(std::vector<std::string>& args) {
    std::vector<char*> result;
    result.reserve(args.size());
    for(auto& item:args)result.push_back(item.data());
    return result;
}
} // namespace galaxy::windows_utf8_argv
#endif
