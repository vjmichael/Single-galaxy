#include "galaxy/FactoryLayout.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace galaxy::industry {
namespace {
constexpr std::int64_t limit=1000000000; // coordinate safety, NOT a factory room-size cap
bool valid_box(const Device& d) {
    return d.id && d.width>0 && d.height>0 &&
        d.origin.x>=-limit && d.origin.y>=-limit &&
        d.origin.x<=limit-d.width && d.origin.y<=limit-d.height;
}
bool overlaps(const Device& a,const Device& b){
    return a.origin.x<b.origin.x+b.width && b.origin.x<a.origin.x+a.width &&
           a.origin.y<b.origin.y+b.height && b.origin.y<a.origin.y+a.height;
}
bool inside(GridCell p,const Device& d){
    return p.x>=d.origin.x && p.x<d.origin.x+d.width &&
           p.y>=d.origin.y && p.y<d.origin.y+d.height;
}
bool next_to(GridCell p,const Device& d){
    if(inside(p,d))return false;
    const bool beside_y=p.y>=d.origin.y && p.y<d.origin.y+d.height &&
      (p.x==d.origin.x-1 || p.x==d.origin.x+d.width);
    const bool beside_x=p.x>=d.origin.x && p.x<d.origin.x+d.width &&
      (p.y==d.origin.y-1 || p.y==d.origin.y+d.height);
    return beside_x || beside_y;
}
bool neighboring(GridCell a,GridCell b){
    return (a.x==b.x && (a.y+1==b.y || b.y+1==a.y)) ||
           (a.y==b.y && (a.x+1==b.x || b.x+1==a.x));
}
bool safe_code(const std::string& code){
    // The v1 portable blueprint format uses these characters as delimiters.
    // The canonical item catalogue may someday admit wider IDs; that requires
    // an explicit blueprint format revision, not silent misparsing.
    return code.find_first_of(",;:|\r\n") == std::string::npos;
}
std::vector<std::string> ordered_codes(std::vector<std::string> codes,const inventory::Catalogue& cat){
    if(codes.empty())throw std::invalid_argument("missing machine ports");
    for(const auto& v:codes)if(!cat.contains(v) || !safe_code(v))throw std::invalid_argument("invalid machine port item");
    std::sort(codes.begin(),codes.end());
    if(std::adjacent_find(codes.begin(),codes.end())!=codes.end())
        throw std::invalid_argument("duplicate machine item port");
    return codes;
}
bool permitted(const Device& d,const std::string& code,bool output){
    if(d.kind==DeviceKind::Storage || d.kind==DeviceKind::Splitter || d.kind==DeviceKind::Merger)return true;
    if(d.kind==DeviceKind::Filter)return d.filter_item==code;
    const auto& codes=output?d.outputs:d.inputs;
    return std::binary_search(codes.begin(),codes.end(),code);
}
std::vector<std::string> fields(const std::string& s,char sep){
    std::vector<std::string> result;std::size_t start=0;
    for(std::size_t i=0;i<=s.size();++i)if(i==s.size() || s[i]==sep){
        result.push_back(s.substr(start,i-start));start=i+1;
    }
    return result;
}
std::int64_t integer(const std::string& s) {
    if(s.empty())throw std::invalid_argument("empty integer");
    std::size_t consumed=0;
    auto n=std::stoll(s,&consumed,10);
    if(consumed!=s.size())throw std::invalid_argument("invalid integer");
    return n;
}
std::string csv(const std::vector<std::string>& v){
    std::string o;for(const auto& s:v){if(!o.empty())o+=',';o+=s;}return o;
}
std::vector<std::string> parse_csv(const std::string& s){return s.empty()?std::vector<std::string>{}:fields(s,',');}
}
FactoryLayout::FactoryLayout(const inventory::Catalogue& catalogue):catalogue_(&catalogue){}
void FactoryLayout::place(Device d){
    if(!valid_box(d)||devices_.count(d.id))throw std::invalid_argument("invalid or duplicate device placement");
    for(const auto& [id,existing]:devices_){(void)id;
        if(overlaps(d,existing))throw std::invalid_argument("device footprint collision");
    }
    for(const auto& [id,route]:conveyors_){(void)id;
        for(const auto& p:route.path)if(inside(p,d))
            throw std::invalid_argument("device collides with placed conveyor");
    }
    devices_.emplace(d.id,std::move(d));
}
void FactoryLayout::place_storage(DeviceId id,GridCell origin,std::int64_t w,std::int64_t h,
                                  std::int64_t capacity){
    if(capacity<=0 || capacity>1000000000000LL)
        throw std::invalid_argument("invalid storage capacity");
    place(Device{id,DeviceKind::Storage,origin,w,h,0,capacity,{},{},{}});
}
void FactoryLayout::place_machine(DeviceId id,GridCell origin,std::uint8_t tier,
       std::vector<std::string> inputs,std::vector<std::string> outputs){
    if(tier<1 || tier>5)throw std::invalid_argument("machine tier out of range");
    const auto side=static_cast<std::int64_t>(1ULL<<tier);
    place(Device{id,DeviceKind::Machine,origin,side,side,tier,1000,
           ordered_codes(std::move(inputs),*catalogue_),
           ordered_codes(std::move(outputs),*catalogue_),{}});
}
void FactoryLayout::place_junction(DeviceId id,DeviceKind kind,GridCell origin,
                                   std::string filter_item){
    if(kind!=DeviceKind::Splitter && kind!=DeviceKind::Merger && kind!=DeviceKind::Filter)
        throw std::invalid_argument("invalid junction kind");
    if((kind==DeviceKind::Filter && (!catalogue_->contains(filter_item) || !safe_code(filter_item))) ||
       (kind!=DeviceKind::Filter && !filter_item.empty()))
        throw std::invalid_argument("invalid filter configuration");
    place(Device{id,kind,origin,1,1,0,1000,{},{},std::move(filter_item)});
}
bool FactoryLayout::reachable(DeviceId start,DeviceId target) const{
    std::set<DeviceId> seen;
    std::vector<DeviceId> pending{start};
    while(!pending.empty()){
        auto id=pending.back();pending.pop_back();
        if(!seen.insert(id).second)continue;
        if(id==target)return true;
        for(const auto& [cid,c]:conveyors_){(void)cid;
            if(c.from==id)pending.push_back(c.to);
        }
    }
    return false;
}
void FactoryLayout::add_conveyor(ConveyorId id,DeviceId from,DeviceId to,
                                 std::string item,std::int64_t rate,std::vector<GridCell> path){
    if(!id || !devices_.count(from) || !devices_.count(to) || from==to ||
       conveyors_.count(id) || !catalogue_->contains(item) || !safe_code(item) || rate<=0 || rate>1000000000 ||
       path.empty() || path.size()>4096 || reachable(to,from))
        throw std::invalid_argument("invalid or cyclic conveyor");
    const auto& a=devices_.at(from),&b=devices_.at(to);
    if(!permitted(a,item,true) || !permitted(b,item,false))
        throw std::invalid_argument("conveyor item incompatible with equipment port");
    if(!next_to(path.front(),a) || !next_to(path.back(),b))
        throw std::invalid_argument("conveyor endpoints do not touch equipment");
    std::set<GridCell> seen;
    for(std::size_t i=0;i<path.size();++i){
        const auto p=path[i];
        if(p.x< -limit || p.x>limit || p.y< -limit || p.y>limit ||
           !seen.insert(p).second || (i && !neighboring(p,path[i-1])))
            throw std::invalid_argument("broken or self-intersecting conveyor path");
        for(const auto& [device_id,d]:devices_){(void)device_id;
            if(inside(p,d))throw std::invalid_argument("conveyor obstructs equipment");
        }
        for(const auto& [route_id,route]:conveyors_){(void)route_id;
            if(std::find(route.path.begin(),route.path.end(),p)!=route.path.end())
                throw std::invalid_argument("conveyor shares occupied ground tile");
        }
    }
    conveyors_.emplace(id,Conveyor{id,from,to,std::move(item),rate,std::move(path)});
}
inventory::LogisticsNetwork FactoryLayout::compile_transport() const{
    inventory::LogisticsNetwork n(*catalogue_);
    for(const auto& [id,d]:devices_)n.create_node(id,d.capacity_liters);
    for(const auto& [id,c]:conveyors_)n.add_route(id,c.from,c.to,c.item_code,c.units_per_tick);
    return n;
}
std::string FactoryLayout::canonical_blueprint() const{
    std::ostringstream out;out<<"single-galaxy-factory-layout-v1\n";
    for(const auto& [id,d]:devices_){
        out<<"D|"<<id<<'|'<<static_cast<int>(d.kind)<<'|'<<static_cast<int>(d.tier)
           <<'|'<<d.origin.x<<'|'<<d.origin.y<<'|'<<d.width<<'|'<<d.height
           <<'|'<<d.capacity_liters<<'|'<<csv(d.inputs)<<'|'<<csv(d.outputs)
           <<'|'<<d.filter_item<<'\n';
    }
    for(const auto& [id,c]:conveyors_){
        out<<"C|"<<id<<'|'<<c.from<<'|'<<c.to<<'|'<<c.item_code<<'|'<<c.units_per_tick<<'|';
        bool first=true;for(const auto& p:c.path){
            if(!first)out<<';';
            first=false;
            out<<p.x<<':'<<p.y;
        }
        out<<'\n';
    }
    return out.str();
}
FactoryLayout FactoryLayout::from_blueprint(const std::string& text,const inventory::Catalogue& c){
    FactoryLayout f(c);
    std::istringstream in(text);std::string line;
    if(!std::getline(in,line) || line!="single-galaxy-factory-layout-v1")
        throw std::invalid_argument("unsupported factory blueprint");
    // All device records precede conveyors. Layout canonicality is checked at end.
    while(std::getline(in,line)){
        if(line.empty())throw std::invalid_argument("empty blueprint record");
        auto p=fields(line,'|');
        if(p[0]=="D" && p.size()==12){
            auto id=static_cast<DeviceId>(integer(p[1]));
            auto kind=integer(p[2]), tier=integer(p[3]);
            GridCell origin{integer(p[4]),integer(p[5])};
            auto w=integer(p[6]),h=integer(p[7]),cap=integer(p[8]);
            if(kind==static_cast<int>(DeviceKind::Storage)){
                if(tier!=0 || !p[9].empty() || !p[10].empty() || !p[11].empty())
                    throw std::invalid_argument("invalid storage record");
                f.place_storage(id,origin,w,h,cap);
            }else if(kind==static_cast<int>(DeviceKind::Machine)){
                if(tier<1 || tier>5 || w!=(1LL<<tier) || h!=w || cap!=1000 || !p[11].empty())
                    throw std::invalid_argument("invalid machine record");
                f.place_machine(id,origin,static_cast<std::uint8_t>(tier),parse_csv(p[9]),parse_csv(p[10]));
            }else if(kind>=static_cast<int>(DeviceKind::Splitter) &&
                     kind<=static_cast<int>(DeviceKind::Merger)){
                if(tier || w!=1 || h!=1 || cap!=1000 || !p[9].empty() || !p[10].empty())
                    throw std::invalid_argument("invalid junction record");
                f.place_junction(id,static_cast<DeviceKind>(kind),origin,p[11]);
            }else throw std::invalid_argument("unknown device type");
        }else if(p[0]=="C" && p.size()==7){
            auto id=static_cast<ConveyorId>(integer(p[1]));
            auto from=static_cast<DeviceId>(integer(p[2])),to=static_cast<DeviceId>(integer(p[3]));
            auto rate=integer(p[5]);
            std::vector<GridCell> cells;
            for(const auto& point:fields(p[6],';')){
                auto xy=fields(point,':');
                if(xy.size()!=2)throw std::invalid_argument("invalid conveyor point");
                cells.push_back({integer(xy[0]),integer(xy[1])});
            }
            f.add_conveyor(id,from,to,p[4],rate,std::move(cells));
        }else throw std::invalid_argument("invalid blueprint record");
    }
    if(f.canonical_blueprint()!=text)throw std::invalid_argument("noncanonical factory blueprint");
    return f;
}
} // namespace galaxy::industry
