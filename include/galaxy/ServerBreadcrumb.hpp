#pragma once
// SGDX-02: host->supervisor breadcrumb only after a successful world commit.
// A lost breadcrumb must never fail a tick or authorize a physical effect.
#include <cstdint>
#ifndef _WIN32
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#endif
namespace galaxy::server_dx {
inline void committed_breadcrumb(std::uint64_t tick,
                                 std::uint64_t generation) noexcept {
#ifndef _WIN32
    const char* number=std::getenv("SGDX_BREADCRUMB_FD");
    if(!number)return;
    // The descriptor is assigned by the parent process, never by the player.
    char* end=nullptr;
    const long fd=std::strtol(number,&end,10);
    if(end==number || *end!='\0' || fd<3 || fd>1024)return;
    char message[128];
    const int length=std::snprintf(message,sizeof(message),"COMMIT|%llu|%llu\n",
        static_cast<unsigned long long>(tick),
        static_cast<unsigned long long>(generation));
    if(length<=0 || length>=static_cast<int>(sizeof(message)))return;
    // Unix datagram socket: MSG_NOSIGNAL means a dead supervisor cannot
    // kill the game with SIGPIPE. Best effort and nonblocking.
    (void)::send(static_cast<int>(fd),message,static_cast<std::size_t>(length),
                 MSG_NOSIGNAL|MSG_DONTWAIT);
#else
    (void)tick;(void)generation; // Windows job telemetry requires a named pipe.
#endif
}
} // namespace galaxy::server_dx
