// ECON-02B2d4c2: localhost UI subprocess. This process is the ONLY bridge
// to PersistentWorld and HostBlueprintRegistry; browser input is always data.
// The launch operator selects the existing SQLite world and (optionally) site.
// A supervising localhost HTTP process provides the authenticated session.
#include "galaxy/HostBlueprintRegistry.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
using namespace galaxy::industry;
namespace {
std::string quote(const std::string& s){
    std::string out="\"";
    constexpr const char* hex="0123456789abcdef";
    for(unsigned char c:s){
        if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else if(c=='\t')out+="\\t";
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
std::uint64_t number(const std::string& text){
    if(text.empty()||text.size()>19)throw std::invalid_argument("invalid unsigned integer");
    std::uint64_t value=0;
    for(const char c:text){
        if(c<'0'||c>'9'||value>(std::numeric_limits<std::uint64_t>::max()-static_cast<unsigned>(c-'0'))/10)
            throw std::invalid_argument("invalid unsigned integer");
        value=value*10+static_cast<unsigned>(c-'0');
    }
    return value;
}
std::string base64(const std::string& encoded){
    if(encoded.empty()||encoded.size()>1400000 || encoded.size()%4)throw std::invalid_argument("invalid blueprint payload length");
    auto digit=[](char c)->int{
        if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;
        if(c>='0'&&c<='9')return c-'0'+52;
        if(c=='+')return 62;if(c=='/')return 63;return -1;
    };
    std::string result;result.reserve(encoded.size()*3/4);
    for(std::size_t i=0;i<encoded.size();i+=4){
        const bool final=i+4==encoded.size();
        const int a=digit(encoded[i]),b=digit(encoded[i+1]);
        const int c=encoded[i+2]=='='?-2:digit(encoded[i+2]);
        const int d=encoded[i+3]=='='?-2:digit(encoded[i+3]);
        if(a<0||b<0||c==-1||d==-1 || (c==-2&&d!=-2) || (!final&&(c==-2||d==-2)) ||
           (c==-2&&(b&15)) || (d==-2&&c>=0&&(c&3)))throw std::invalid_argument("invalid blueprint encoding");
        const std::uint32_t bits=(static_cast<std::uint32_t>(a)<<18)|(static_cast<std::uint32_t>(b)<<12)|
          (static_cast<std::uint32_t>(std::max(c,0))<<6)|static_cast<std::uint32_t>(std::max(d,0));
        result+=static_cast<char>((bits>>16)&255);
        if(c>=0)result+=static_cast<char>((bits>>8)&255);
        if(d>=0)result+=static_cast<char>(bits&255);
    }
    if(result.size()>1048576)throw std::invalid_argument("blueprint exceeds host limit");
    return result;
}
std::string status(const HostedPlanStatus& p){
    const auto& a=p.progress;
    return "{\"id\":"+std::to_string(p.plan_id)+",\"site\":"+std::to_string(p.site)+
       ",\"completed\":"+std::to_string(a.completed)+",\"total\":"+std::to_string(a.total)+
       ",\"modulesRemaining\":"+std::to_string(a.modules_remaining)+
       ",\"cancelled\":"+(a.cancelled?"true":"false")+
       ",\"conflicting\":"+(a.conflicting?"true":"false")+
       ",\"finished\":"+(a.finished()?"true":"false")+
       ",\"reason\":"+quote(a.reason)+"}";
}
std::string event(const Event& e){
    return "{\"commandId\":"+std::to_string(e.command_id)+",\"tick\":"+std::to_string(e.tick)+
           ",\"applied\":"+(e.result==Outcome::Applied?"true":"false")+
           ",\"result\":"+std::to_string(static_cast<int>(e.result))+"}";
}
struct Server {
    explicit Server(const std::string& path,std::uint64_t requested_site):world(path,Simulation(1)),host(path,world){
        const auto& sites=world.world().built_outposts();
        if(sites.empty())throw std::runtime_error("World has no constructed outpost; no new colony will be fabricated");
        if(!requested_site){
            if(sites.size()!=1)throw std::runtime_error("More than one outpost; specify --site N in trusted launcher");
            site=sites.begin()->first;
        }else site=requested_site;
        auto it=sites.find(site);if(it==sites.end())throw std::runtime_error("Selected site does not exist");
        ship=it->second.source_colony_ship;
        const auto* actor=world.world().find(ship);
        if(!actor||!world.world().colony_ship(ship)||actor->owner_id==0)throw std::runtime_error("Invalid founding colony ship");
        owner=actor->owner_id;
    }
    std::string handle(const std::string& input){
        const auto sep=input.find('|');const std::string op=input.substr(0,sep);
        const std::string arg=sep==std::string::npos?"":input.substr(sep+1);
        if(op=="HELLO"){
            const auto* c=world.world().colony_ship(ship);
            return "{\"ok\":true,\"site\":"+std::to_string(site)+",\"ship\":"+std::to_string(ship)+
                   ",\"modulesAvailable\":"+std::to_string(c?c->modules:0)+",\"tick\":"+std::to_string(world.world().tick())+"}";
        }
        if(op=="LIST"){
            const auto plans=host.list(owner);std::string out="{\"ok\":true,\"plans\":[";
            for(std::size_t i=0;i<plans.size();++i){if(i)out+=',';out+=status(plans[i]);}
            return out+"]}";
        }
        if(op=="REGISTER"){
            const auto id=host.register_layout(base64(arg),owner,ship,site);
            return "{\"ok\":true,\"plan\":"+status(host.status(id,owner))+"}";
        }
        if(op=="STATUS"){
            const auto id=number(arg);
            if(id>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))throw std::invalid_argument("invalid plan ID");
            return "{\"ok\":true,\"plan\":"+status(host.status(static_cast<std::int64_t>(id),owner))+"}";
        }
        if(op=="CANCEL"){
            const auto id=number(arg);
            if(id>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))throw std::invalid_argument("invalid plan ID");
            const bool did=host.cancel(static_cast<std::int64_t>(id),owner);
            return std::string("{\"ok\":true,\"changed\":")+(did?"true":"false")+
                   ",\"plan\":"+status(host.status(static_cast<std::int64_t>(id),owner))+"}";
        }
        if(op=="STEP"){
            const auto id=number(arg);
            if(id>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))throw std::invalid_argument("invalid plan ID");
            const auto e=host.execute_one(static_cast<std::int64_t>(id),owner);
            return "{\"ok\":true,\"event\":"+event(e)+
                   ",\"plan\":"+status(host.status(static_cast<std::int64_t>(id),owner))+"}";
        }
        throw std::invalid_argument("unknown host request");
    }
    PersistentWorld world;
    HostBlueprintRegistry host;
    PlayerId owner=0;
    EntityId ship=0,site=0;
};
}
int main(int argc,char** argv){
    if(argc!=2 && argc!=4){std::cerr<<"Usage: galaxy_factory_host_bridge EXISTING-world.sqlite [--site outpost-ID]\n";return 2;}
    try{
        const std::filesystem::path path=argv[1];
        if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)==0)
            throw std::runtime_error("Host requires an existing nonempty world database");
        if(argc==4&&std::string(argv[2])!="--site")throw std::invalid_argument("expected --site argument");
        Server server(path.string(),argc==4?number(argv[3]):0);
        std::string line;
        while(std::getline(std::cin,line)){
            try{
                if(line.size()>1500000)throw std::invalid_argument("request too large");
                std::cout<<server.handle(line)<<'\n'<<std::flush;
            }catch(const std::exception& e){
                std::cout<<"{\"ok\":false,\"error\":"<<quote(e.what())<<"}\n"<<std::flush;
            }
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"Factory host refused startup: "<<e.what()<<'\n';return 1;}
}
