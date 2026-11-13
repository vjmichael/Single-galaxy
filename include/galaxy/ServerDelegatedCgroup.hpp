#pragma once
// S6-GK: Linux cgroup-v2 custody underneath a separately owned systemd unit.
// This is an OPTIONAL stronger boundary. It NEVER self-creates a root service,
// delegates system cgroups, mutates cgroup permissions, or claims that a
// process-group test establishes kernel ancestor-migration containment.
#ifdef __linux__
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <unistd.h>

namespace galaxy::server_dx {
class DelegatedCgroup final {
    int parent_=-1;
    int leaf_=-1;
    std::string name_;
    bool established_=false;
    static void closefd(int& fd) noexcept {if(fd>=0){::close(fd);fd=-1;}}
    static std::string self_cgroup(){
        std::ifstream in("/proc/self/cgroup");
        std::string line;
        while(std::getline(in,line))
            if(line.rfind("0::/",0)==0) return line.substr(3);
        throw std::runtime_error("S6-GK: no verified unified cgroup-v2 membership");
    }
    static int open_member(int root,const char* name,int flags){
        const int fd=::openat(root,name,flags|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0)throw std::runtime_error(std::string("S6-GK: missing delegated ")+name+
                                         ": "+std::strerror(errno));
        return fd;
    }
    static void write_all(int fd,const std::string& value,const char* label){
        const auto n=::write(fd,value.data(),value.size());
        if(n<0 || static_cast<std::size_t>(n)!=value.size())
            throw std::runtime_error(std::string("S6-GK: ")+label+" refused by kernel");
    }
    static bool populated(int dir){
        const int fd=open_member(dir,"cgroup.events",O_RDONLY);
        char bytes[256]{};
        const auto n=::read(fd,bytes,sizeof(bytes)-1);
        ::close(fd);
        if(n<=0)throw std::runtime_error("S6-GK: no cgroup.events evidence");
        const std::string s(bytes,static_cast<std::size_t>(n));
        if(s.find("populated 1\n")!=std::string::npos)return true;
        if(s.find("populated 0\n")!=std::string::npos)return false;
        throw std::runtime_error("S6-GK: malformed cgroup.events");
    }
public:
    DelegatedCgroup()=default;
    DelegatedCgroup(const DelegatedCgroup&)=delete;
    DelegatedCgroup& operator=(const DelegatedCgroup&)=delete;
    explicit DelegatedCgroup(const std::string& root){
        if(root.empty() || root[0]!='/' || root.size()>4096)
            throw std::runtime_error("S6-GK: require absolute existing delegated cgroup root");
        const auto membership=self_cgroup();
        if(membership=="/" || membership.find("..")!=std::string::npos)
            throw std::runtime_error("S6-GK: cannot prove an owned systemd outer unit");
        // Require the supervisor itself to be in the named root. Never allow
        // writing to arbitrary cgroup2 ancestors or unrelated services.
        const auto expected=std::filesystem::canonical(
            "/sys/fs/cgroup"+membership);
        const auto candidate=std::filesystem::canonical(root);
        if(candidate!=expected)
            throw std::runtime_error("S6-GK: root is not this supervisor's own unit");
        parent_=::open(candidate.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(parent_<0)throw std::runtime_error("S6-GK: cannot open delegated unit");
        try{
            struct statfs info{};
            if(::fstatfs(parent_,&info)!=0 ||
               static_cast<unsigned long>(info.f_type)!=CGROUP2_SUPER_MAGIC)
                throw std::runtime_error("S6-GK: not a native cgroup-v2 mount");
            // Only the newly owned LEAF needs cgroup.kill write authority.
            // systemd does not delegate parent-unit cgroup.kill to service users;
            // requiring it would demand excessive control over the outer unit.
            name_="sgdx-child-"+std::to_string(::getpid());
            if(::mkdirat(parent_,name_.c_str(),0700)!=0)
                throw std::runtime_error("S6-GK: cannot create unique delegated leaf");
            established_=true;
            leaf_=::openat(parent_,name_.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
            if(leaf_<0)throw std::runtime_error("S6-GK: cannot open created leaf");
            int fd=open_member(leaf_,"cgroup.procs",O_WRONLY);closefd(fd);
            fd=open_member(leaf_,"cgroup.kill",O_WRONLY);closefd(fd);
            if(populated(leaf_))throw std::runtime_error("S6-GK: leaf contains an unowned process");
        }catch(...){
            cleanup_noexcept();throw;
        }
    }
    void close_in_child() noexcept {closefd(leaf_);closefd(parent_);}
    void attach(pid_t pid){
        if(pid<=0 || leaf_<0)throw std::runtime_error("S6-GK: invalid attach");
        const int fd=open_member(leaf_,"cgroup.procs",O_WRONLY);
        try{write_all(fd,std::to_string(pid),"child attach");}catch(...){::close(fd);throw;}
        ::close(fd);
        // Read the kernel's live membership, not a synthetic receipt.
        const auto observed=std::filesystem::path("/proc")/std::to_string(pid)/"cgroup";
        std::ifstream in(observed);std::string line;
        const auto expected=self_cgroup()+"/"+name_;
        bool found=false;
        while(std::getline(in,line))if(line=="0::"+expected){found=true;break;}
        if(!found || !populated(leaf_))
            throw std::runtime_error("S6-GK: kernel did not attest worker in delegated leaf");
    }
    void kill_all(){
        if(leaf_<0)throw std::runtime_error("S6-GK: no delegated leaf for kill");
        const int fd=open_member(leaf_,"cgroup.kill",O_WRONLY);
        try{write_all(fd,"1","cgroup.kill");}catch(...){::close(fd);throw;}
        ::close(fd);
    }
    void finish(){
        if(!established_)return;
        kill_all();
        for(int attempt=0;attempt<100;++attempt){
            if(!populated(leaf_))break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if(populated(leaf_))throw std::runtime_error("S6-GK: survivor in delegated leaf");
        closefd(leaf_);
        if(::unlinkat(parent_,name_.c_str(),AT_REMOVEDIR)!=0)
            throw std::runtime_error("S6-GK: could not remove empty leaf");
        established_=false;
        closefd(parent_);
    }
    void cleanup_noexcept() noexcept {
        if(established_ && leaf_>=0){
            const int fd=::openat(leaf_,"cgroup.kill",O_WRONLY|O_CLOEXEC|O_NOFOLLOW);
            if(fd>=0){(void)::write(fd,"1",1);::close(fd);}
        }
        closefd(leaf_);
        if(established_ && parent_>=0)(void)::unlinkat(parent_,name_.c_str(),AT_REMOVEDIR);
        established_=false;closefd(parent_);
    }
    ~DelegatedCgroup() noexcept {cleanup_noexcept();}
};
} // namespace galaxy::server_dx
#endif
