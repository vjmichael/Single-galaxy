#include "galaxy/BlueprintConstruction.hpp"
#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace galaxy::industry {
namespace {
constexpr DeviceId kWorkshop=kWorkshopConveyorNode;
constexpr std::int64_t kWorldGrid=1000000;
std::uint32_t node(DeviceId id){
    if(id==kWorkshop)return kWorkshopConveyorNode;
    if(id<1 || id>static_cast<DeviceId>(std::numeric_limits<int>::max()/4)+1)
        throw std::invalid_argument("invalid blueprint storage device ID");
    return static_cast<std::uint32_t>(id-1);
}
int safe_coord(std::int64_t x){
    if(x<-kWorldGrid || x>kWorldGrid)
        throw std::invalid_argument("blueprint position outside game construction grid");
    return static_cast<int>(x);
}
int good_index(const std::string& item){
    if(item=="good/aluminum")return 0;
    if(item=="good/aluminum_panels")return 1;
    if(item=="good/aluminum_frames")return 2;
    if(item=="good/aluminum_scrap")return 3;
    throw std::invalid_argument("item not supported by installed transport backend");
}
std::uint64_t natural(const std::string& s){
    if(s.empty() || s[0]=='-' || s[0]=='+')throw std::invalid_argument("invalid unsigned manifest value");
    std::size_t n=0;auto v=std::stoull(s,&n);if(n!=s.size())throw std::invalid_argument("invalid unsigned manifest value");
    return v;
}
}
BlueprintConstruction::BlueprintConstruction(FactoryLayout bp,EntityId site,EntityId ship,
           PlayerId owner,std::uint64_t first):blueprint_(std::move(bp)),site_(site),ship_(ship),owner_(owner),base_(first){
    if(!site || !ship || !owner || owner==Simulation::kServerAuthority || !base_ ||
       blueprint_.devices().empty() || !blueprint_.devices().count(1))
        throw std::invalid_argument("invalid identity or missing base storage in factory plan");
    compile();
}
void BlueprintConstruction::compile(){
    const auto& devices=blueprint_.devices();
    const auto& links=blueprint_.conveyors();
    if(devices.size()>256 || links.size()>256)
        throw std::invalid_argument("blueprint exceeds bounded construction batch");
    const auto& base_storage=devices.at(1);
    if(base_storage.kind!=DeviceKind::Storage || base_storage.width!=1 ||
       base_storage.height!=1 || base_storage.capacity_liters!=4000)
        throw std::invalid_argument("base storage device must match installed bin");
    for(const auto& [id,d]:devices){
        (void)node(id);safe_coord(d.origin.x);safe_coord(d.origin.y);
        if(d.kind==DeviceKind::Storage){
            if(d.width!=1 || d.height!=1 || d.capacity_liters!=4000)
                throw std::invalid_argument("world currently supports 4m3, 1x1 storage");
        }else if(d.kind==DeviceKind::Machine){
            if(id!=kWorkshop || d.tier!=1 || d.width!=2 || d.height!=2 ||
               d.inputs!=std::vector<std::string>{"good/aluminum"} ||
               d.outputs!=std::vector<std::string>{"good/aluminum_frames","good/aluminum_panels","good/aluminum_scrap"})
                throw std::invalid_argument("world currently supports one Tier-1 workshop");
        }else throw std::invalid_argument("splitters/filters/mergers lack resource-backed world installation");
    }
    std::size_t total=2*devices.size()+1;
    for(const auto& [id,link]:links){
        (void)id;
        if(link.path.size()>4096 || link.units_per_tick<1 || link.units_per_tick>1000)
            throw std::invalid_argument("transport rate or route length exceeds world limits");
        if(total>10000-link.path.size()-2)throw std::invalid_argument("construction plan too large");
        total+=link.path.size()+2;
        for(const auto& p:link.path){safe_coord(p.x);safe_coord(p.y);}
        (void)good_index(link.item_code);
        if(link.from==kWorkshop && link.to==kWorkshop)
            throw std::invalid_argument("workshop cannot feed itself");
    }
    if(base_>std::numeric_limits<std::uint64_t>::max()-total-1)
        throw std::invalid_argument("plan reference overflow");
    std::uint64_t next=base_;
    auto push=[&](Action action,std::int64_t amount,int x,int y,std::uint8_t cost,
                  std::uint64_t source,std::string label){
        Command c{};c.id=++next;c.sequence=c.id;c.issuer=owner_;c.actor=ship_;
        c.target=site_;c.action=action;c.amount=amount;c.thrust_x=x;c.thrust_y=y;
        jobs_.push_back(BuildJob{c,std::move(label),source,cost});
        return c.id;
    };
    // All fabricated devices predate placement. Previously completed world
    // equipment is recognized when the plan is resumed.
    push(Action::ConstructStorage,1,0,0,1,1,"construct base container");
    for(const auto& [id,d]:devices)if(id>1 && d.kind==DeviceKind::Storage)
        push(Action::ConstructStorageNode,static_cast<std::int64_t>(node(id)),0,0,1,id,"construct storage node");
    if(devices.count(kWorkshop))
        push(Action::ConstructWorkshop,1,0,0,1,kWorkshop,"construct Tier-1 workshop");
    for(const auto& [id,d]:devices)
        push(Action::PlaceFactoryEquipment,id==kWorkshop?-1:static_cast<std::int64_t>(node(id)),
             safe_coord(d.origin.x),safe_coord(d.origin.y),0,id,"install equipment at blueprint position");
    for(const auto& [id,link]:links){
        const int item=good_index(link.item_code);
        const auto from=node(link.from),to=node(link.to);
        const bool workshop=(from==kWorkshop || to==kWorkshop);
        std::uint64_t installed_link=0;
        if(workshop){
            const auto storage=from==kWorkshop?to:from;
            installed_link=push(Action::ConstructWorkshopLink,link.units_per_tick*4+item,
                static_cast<int>(storage),from==kWorkshop?1:0,1,id,"construct workshop transport link");
        }else{
            installed_link=push(Action::ConstructStorageLink,link.units_per_tick*4+item,
                static_cast<int>(from),static_cast<int>(to),1,id,"construct container transport link");
        }
        for(const auto& p:link.path)
            push(Action::PlaceConveyorCell,static_cast<std::int64_t>(installed_link),
                safe_coord(p.x),safe_coord(p.y),1,id,"install conveyor segment");
        push(Action::CompleteConveyorRoute,static_cast<std::int64_t>(installed_link),0,0,0,id,
             "activate completed conveyor");
    }
    if(jobs_.size()>10000)throw std::invalid_argument("construction plan too large");
}
std::string BlueprintConstruction::manifest() const {
    std::ostringstream s;
    s<<"single-galaxy-construction-plan-v1|"<<site_<<'|'<<ship_<<'|'<<owner_<<'|'<<base_<<'|'<<(cancelled_?1:0)<<'\n';
    s<<blueprint_.canonical_blueprint();return s.str();
}
BlueprintConstruction BlueprintConstruction::from_manifest(const std::string& s){
    const auto p=s.find('\n');if(p==std::string::npos)throw std::invalid_argument("missing plan header");
    std::vector<std::string> fields;std::stringstream in(s.substr(0,p));std::string x;
    while(std::getline(in,x,'|'))fields.push_back(x);
    if(fields.size()!=6 || fields[0]!="single-galaxy-construction-plan-v1" ||
       (fields[5]!="0" && fields[5]!="1"))
        throw std::invalid_argument("unsupported construction plan");
    auto bp=FactoryLayout::from_blueprint(s.substr(p+1));
    BlueprintConstruction plan(std::move(bp),natural(fields[1]),natural(fields[2]),
                               natural(fields[3]),natural(fields[4]));
    plan.cancelled_=fields[5]=="1";
    if(plan.manifest()!=s)throw std::invalid_argument("noncanonical construction manifest");
    return plan;
}
const OutpostStorageLink* BlueprintConstruction::find_link(const Simulation& world,
        std::uint64_t blueprint_id) const {
    const auto* st=world.storage(site_);
    if(!st)return nullptr;
    const auto& planned=blueprint_.conveyors().at(blueprint_id);
    const auto src=node(planned.from),dst=node(planned.to);
    const int item=good_index(planned.item_code);
    const OutpostStorageLink* match=nullptr;
    for(const auto& [id,link]:st->links){
        (void)id;
        if(link.source==src && link.destination==dst && link.item_index==item){
            if(link.units_per_tick!=planned.units_per_tick || match)
                throw std::runtime_error("outpost link topology conflicts with saved plan");
            match=&link;
        }
    }
    if(match && match->route_cells.size()>planned.path.size())
        throw std::runtime_error("installed conveyor extends beyond saved plan");
    if(match){
        for(std::size_t i=0;i<match->route_cells.size();++i){
            const auto& a=match->route_cells[i];const auto& b=planned.path[i];
            if(a.x!=b.x || a.y!=b.y)
                throw std::runtime_error("installed conveyor deviates from saved plan");
        }
        if(match->route_ready && match->route_cells.size()!=planned.path.size())
            throw std::runtime_error("completed conveyor has wrong route length");
    }
    return match;
}
bool BlueprintConstruction::job_done(const BuildJob& j,const Simulation& world) const {
    const auto& c=j.command;
    const auto* st=world.storage(site_);
    switch(c.action){
    case Action::ConstructStorage: return st!=nullptr;
    case Action::ConstructStorageNode: return st && st->extra_bins.count(static_cast<std::uint32_t>(c.amount));
    case Action::ConstructWorkshop: return world.workshop(site_)!=nullptr;
    case Action::PlaceFactoryEquipment:{
        if(!st)return false;
        const auto node_id=c.amount==-1?kWorkshop:static_cast<std::uint32_t>(c.amount);
        const auto p=st->equipment_positions.find(node_id);
        if(p!=st->equipment_positions.end()){
            if(p->second.x!=c.thrust_x || p->second.y!=c.thrust_y)
                throw std::runtime_error("equipment occupies different coordinates than blueprint");
            return true;
        }
        return false;
    }
    case Action::ConstructStorageLink:
    case Action::ConstructWorkshopLink:
        return find_link(world,j.blueprint_device_or_link)!=nullptr;
    case Action::PlaceConveyorCell:{
        auto link=find_link(world,j.blueprint_device_or_link);
        if(!link)return false;
        std::size_t sequence=0;
        for(const auto& prev:jobs_){
            if(&prev==&j)break;
            if(prev.command.action==Action::PlaceConveyorCell &&
               prev.blueprint_device_or_link==j.blueprint_device_or_link)++sequence;
        }
        return link->route_cells.size()>sequence;
    }
    case Action::CompleteConveyorRoute:{
        auto link=find_link(world,j.blueprint_device_or_link);
        return link && link->route_ready;
    }
    default: throw std::logic_error("unexpected blueprint build command");
    }
}
ConstructionProgress BlueprintConstruction::inspect(const Simulation& world) const {
    ConstructionProgress status{};status.total=jobs_.size();status.cancelled=cancelled_;
    try{
        const auto site=world.built_outposts().find(site_);
        const auto ship=world.find(ship_);
        if(site==world.built_outposts().end() || site->second.source_colony_ship!=ship_ ||
           !ship || ship->owner_id!=owner_ || !world.colony_ship(ship_))
            throw std::runtime_error("plan actor is not the owner of an existing colony outpost");
        const auto* st=world.storage(site_);
        const auto* ws=world.workshop(site_);
        if((st && st->source_ship!=ship_) || (ws && ws->source_ship!=ship_))
            throw std::runtime_error("outpost equipment belongs to another colony ship");
        bool pending_seen=false;
        for(const auto& job:jobs_){
            const bool done=job_done(job,world);
            if(done){
                if(pending_seen)throw std::runtime_error("out-of-order construction conflicts with plan");
                ++status.completed;
            }else{
                pending_seen=true;
                status.modules_remaining+=job.module_cost;
            }
        }
    }catch(const std::exception& e){status.conflicting=true;status.reason=e.what();}
    return status;
}
Event BlueprintConstruction::execute_next(PersistentWorld& w,PlayerId owner,EntityId ship,
                                           std::uint64_t fresh_id) const {
    if(cancelled_)throw std::runtime_error("construction plan was cancelled");
    if(owner!=owner_ || ship!=ship_)
        throw std::invalid_argument("plan identity does not match authenticated authorizer");
    const auto s=inspect(w.world());
    if(s.conflicting || s.finished())throw std::runtime_error("construction complete or conflicting: "+s.reason);
    if(!fresh_id || fresh_id>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("fresh positive signed-range command ID required");
    const auto& planned=jobs_.at(s.completed);
    auto c=planned.command;
    c.id=fresh_id;c.sequence=fresh_id;
    if(c.action==Action::PlaceConveyorCell || c.action==Action::CompleteConveyorRoute){
        const auto link=find_link(w.world(),planned.blueprint_device_or_link);
        if(!link)throw std::runtime_error("conveyor link missing before segment installation");
        c.amount=static_cast<std::int64_t>(link->id);
    }
    c.execute_tick=w.world().tick()+1;
    if(!w.submit(c))throw std::runtime_error("host rejected new construction job submission");
    w.step();
    if(w.world().events().empty() || w.world().events().back().command_id!=c.id)
        throw std::runtime_error("construction command receipt missing");
    return w.world().events().back();
}
} // namespace galaxy::industry
