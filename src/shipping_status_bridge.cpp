// DEMOG-05q6e5b8j: the only remote path into a canonical freight world is the
// private stdin/stdout pipe inherited by the authenticated loopback host.
// The process may run in read-only mode, or as the EXCLUSIVE standalone game
// writer when explicitly provisioned with the region configuration. Never
// launch a second writer against an actively running galaxy host.
#include "galaxy/ShipmentStatusView.hpp"
#include "galaxy/ShipmentManifest.hpp"
#include "galaxy/CanonicalShippingHost.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace re=galaxy::regional_events;
namespace ind=galaxy::industry;
namespace ship=galaxy::shipping;
namespace {
bool token(const std::string& s){return !s.empty()&&s.size()<=100&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos;}
std::uint64_t positive(const std::string& s){if(s.empty()||s.size()>18||s[0]=='0'||s.find_first_not_of("0123456789")!=std::string::npos)throw std::invalid_argument("invalid positive identity");const auto n=std::stoull(s);if(!n)throw std::invalid_argument("zero identity");return n;}
std::vector<std::string> split(const std::string& s){std::vector<std::string> out;std::size_t pos=0;while(true){auto i=s.find('|',pos);out.push_back(s.substr(pos,i==std::string::npos?std::string::npos:i-pos));if(i==std::string::npos)break;pos=i+1;if(out.size()>12)throw std::invalid_argument("oversized protocol");}return out;}
std::vector<std::string> families(const std::string& s){std::vector<std::string> out;std::size_t p=0;while(true){auto i=s.find(',',p);auto part=s.substr(p,i==std::string::npos?std::string::npos:i-p);if(!token(part))throw std::invalid_argument("invalid family scope");out.push_back(part);if(i==std::string::npos)break;p=i+1;}return out;}
// An advisory lease prevents two instances of THIS native shipping writer
// from serving the same world concurrently. Legacy game hosts have not yet
// adopted this lock: operator must stop them before enabling writer mode.
struct WriterLease {
#ifdef _WIN32
 HANDLE handle=INVALID_HANDLE_VALUE;
 explicit WriterLease(const std::string& p){
  handle=CreateFileA(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("another shipping writer may own this world");
 }
 ~WriterLease(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
#else
 int fd=-1;
 explicit WriterLease(const std::string& p){
  int flags=O_CREAT|O_RDWR;
#ifdef O_CLOEXEC
  flags|=O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
  flags|=O_NOFOLLOW;
#endif
  fd=::open(p.c_str(),flags,0600);
  if(fd<0)throw std::runtime_error("cannot open world writer lock");
  struct stat st{};
  if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||flock(fd,LOCK_EX|LOCK_NB)!=0){close(fd);fd=-1;throw std::runtime_error("world already has a shipping writer");}
 }
 ~WriterLease(){if(fd>=0){flock(fd,LOCK_UN);close(fd);}}
#endif
 WriterLease(const WriterLease&)=delete;WriterLease& operator=(const WriterLease&)=delete;
};
struct Writer {
 WriterLease lease;
 galaxy::PlayerId active=0;
 galaxy::PersistentWorld world;
 re::CheckpointStore checkpoint;
 re::RegionalModeAuthority authority;
 ind::RegionalProductionNetwork network;
 ship::ThirdPartyRegistry registry;
 ship::CanonicalShippingHost host;
 Writer(const std::string& database,std::uint64_t seed,const std::string& region,
        const std::string& colony,const std::vector<std::string>& family)
 :lease(std::filesystem::canonical(database).string()+".host.lock"),
  world(database,galaxy::Simulation(seed)),checkpoint(database,region),authority(checkpoint),
  network(world,checkpoint,authority,database,colony,family),
  registry(world,database,[this](galaxy::PlayerId p){return active!=0&&active==p;}),
  host(world,checkpoint,authority,network,registry,database,
       [this](galaxy::PlayerId p){return active!=0&&active==p;}) {host.verify();}
 void run(const std::vector<std::string>& p){
  if(p.size()<3||p.size()>7)throw std::invalid_argument("malformed host command");
  active=positive(p[1]);
  struct Clear {galaxy::PlayerId& p;~Clear(){p=0;}} clear{active};
  const auto& name=p[0],&id=p[2];if(!token(id))throw std::invalid_argument("invalid shipment ID");
  if(name=="APPROVE"&&p.size()==4)host.approve_booking(id,active,static_cast<int>(positive(p[3])));
  else if(name=="REVOKE"&&p.size()==3)host.revoke_booking(id,active);
  else if(name=="DISPATCH"&&p.size()==4&&token(p[3]))host.dispatch(id,active,p[3]);
  else if(name=="LOAD"&&p.size()==3)host.load(id,active);
  else if(name=="UNLOAD"&&p.size()==3)host.unload(id,active);
  else if(name=="ACK"&&p.size()==4)host.acknowledge_delivery(id,active,static_cast<int>(positive(p[3])));
  else if(name=="REQUEST_RETURN"&&p.size()==4)host.request_return(id,active,positive(p[3]));
  else if(name=="APPROVE_RETURN"&&p.size()==4)host.approve_return(id,active,static_cast<int>(positive(p[3])));
  else if(name=="RETURN"&&p.size()==3)host.return_to_origin(id,active);
  else if(name=="APPROVE_REDIRECT"&&p.size()==4)host.approve_redirect(id,active,static_cast<int>(positive(p[3])));
  else if(name=="DELIVER_REDIRECT"&&p.size()==3)host.deliver_redirected(id,active);
  else if(name=="ACK_REDIRECT"&&p.size()==4)host.acknowledge_redirected_delivery(id,active,static_cast<int>(positive(p[3])));
  else throw std::invalid_argument("unknown or malformed freight host operation");
 }
};
}
int main(int argc,char** argv){
 if(argc!=2 && argc!=7){std::cerr<<"Usage: galaxy_shipping_status_bridge WORLD.sqlite [--writer SEED REGION COLONY FAMILY1,FAMILY2]\n";return 2;}
 try{
  const std::filesystem::path db=argv[1];
  if(!std::filesystem::is_regular_file(db)||std::filesystem::file_size(db)==0)
   throw std::runtime_error("existing authoritative world database required");
  ship::ShipmentStatusView view(db.string());
  std::unique_ptr<Writer> writer;
  if(argc==7){if(std::string(argv[2])!="--writer"||!token(argv[4])||!token(argv[5]))throw std::invalid_argument("invalid writer arguments");
   writer=std::make_unique<Writer>(db.string(),positive(argv[3]),argv[4],argv[5],families(argv[6]));}
  std::string line;
  while(std::getline(std::cin,line)){
   try{
    if(line.size()>650)throw std::invalid_argument("oversized host request");
    const auto p=split(line);
    if(p.size()==1&&p[0]=="CAPS"){
     std::cout<<"{\"ok\":true,\"writes\":"<<(writer?"true":"false")<<"}\n"<<std::flush;continue;
    }
    if(p.size()==2&&p[0]=="LIST"){
     const auto principal=positive(p[1]);
     ship::verify_third_party_registry(db.string());
     std::cout<<ship::ShipmentStatusView::to_json(view.for_principal(principal))<<'\n'<<std::flush;continue;
    }
    if(!writer)throw std::runtime_error("shipping host is read-only");
    writer->run(p);
    ship::verify_third_party_registry(db.string());
    std::cout<<"{\"ok\":true}\n"<<std::flush;
   }catch(const std::exception&){
    std::cout<<"{\"ok\":false,\"error\":\"Request refused by canonical game host\"}\n"<<std::flush;
   }
  }
 }catch(const std::exception& e){std::cerr<<"Shipping bridge refused startup: "<<e.what()<<'\n';return 1;}
}
