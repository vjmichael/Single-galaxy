#pragma once
// PLAY-STATION-02: process-lifetime custody of a local original-world save.
// SQLite's short transaction locks protect individual writes; this OS lock
// additionally prevents two independent native hosts from running stale
// in-memory Simulation snapshots against the same save between transactions.
// The sidecar lock file is intentionally retained. Removing it would permit
// a second inode to evade POSIX locking. The OS releases the lock on exit.
#include <filesystem>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace galaxy {
class NativeWriterLease final {
public:
    explicit NativeWriterLease(std::filesystem::path world_save) {
        // The original live-colony host uses .host.lock; the native desktop
        // station/cockpit hosts use .sg-native-writer.lock. Hold BOTH locks
        // for the complete process lifetime to exclude cross-host stale
        // Simulation snapshots between otherwise atomic SQLite commits.
        auto colony_lock=std::filesystem::weakly_canonical(world_save);
        colony_lock += ".host.lock";
        world_save += ".sg-native-writer.lock";
#ifdef _WIN32
        handle_ = ::CreateFileW(world_save.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("native writer already active or lock inaccessible");
        colony_handle_ = ::CreateFileW(colony_lock.c_str(), GENERIC_READ | GENERIC_WRITE,
                                      0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(colony_handle_==INVALID_HANDLE_VALUE){
            ::CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;
            throw std::runtime_error("original colony host writer already active or lock inaccessible");
        }
#else
        fd_ = ::open(world_save.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd_ < 0)
            throw std::runtime_error("native writer lock file inaccessible");
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            const int error = errno;
            ::close(fd_);
            fd_ = -1;
            (void)error;
            throw std::runtime_error("native writer already active or lock inaccessible");
        }
        int flags=O_CREAT | O_RDWR | O_CLOEXEC;
#ifdef O_NOFOLLOW
        flags|=O_NOFOLLOW;
#endif
        colony_fd_=::open(colony_lock.c_str(), flags, 0600);
        if(colony_fd_<0 || ::flock(colony_fd_,LOCK_EX|LOCK_NB)!=0){
            if(colony_fd_>=0){::close(colony_fd_);colony_fd_=-1;}
            ::flock(fd_,LOCK_UN);::close(fd_);fd_=-1;
            throw std::runtime_error("original colony host writer already active or lock inaccessible");
        }
#endif
    }
    NativeWriterLease(const NativeWriterLease&) = delete;
    NativeWriterLease& operator=(const NativeWriterLease&) = delete;
    ~NativeWriterLease() {
#ifdef _WIN32
        if (colony_handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(colony_handle_);
        if (handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_);
#else
        if(colony_fd_>=0){::flock(colony_fd_,LOCK_UN);::close(colony_fd_);}
        if (fd_ >= 0) {::flock(fd_, LOCK_UN);::close(fd_);}
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE colony_handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
    int colony_fd_ = -1;
#endif
};
} // namespace galaxy
