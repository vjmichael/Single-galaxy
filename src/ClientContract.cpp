#include "galaxy/ClientContract.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace galaxy {
namespace {
constexpr std::size_t kMaxFrameBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxChanges = 60000;

bool same(const Entity& a, const Entity& b) {
    return a.id == b.id && a.system_id == b.system_id && a.type == b.type &&
           a.owner_id == b.owner_id && a.position.x == b.position.x && a.position.y == b.position.y &&
           a.velocity.x == b.velocity.x && a.velocity.y == b.velocity.y && a.ore == b.ore &&
           a.cargo == b.cargo && a.cargo_capacity == b.cargo_capacity;
}
bool same(const JumpTelemetry& a, const JumpTelemetry& b) {
    return a.state==b.state && a.gate==b.gate && a.remaining_ticks==b.remaining_ticks;
}
bool same(const DockTelemetry& a, const DockTelemetry& b) {
    return a.state==b.state && a.station==b.station && a.remaining_ticks==b.remaining_ticks;
}
bool same(const StationMarket& a, const StationMarket& b) {
    return a.ore==b.ore && a.credits==b.credits && a.price_per_ore==b.price_per_ore;
}
bool same(const StationHangar& a, const StationHangar& b) {
    return a.ore==b.ore && a.capacity==b.capacity;
}
bool same(const AutoMining& a, const AutoMining& b) {
    return a.target == b.target && a.amount_per_tick == b.amount_per_tick &&
           a.start_command_id == b.start_command_id;
}
// Owner-specific interest projection. No remote-system entities, inventories,
// market inventories, or another owner's private hangars/wallets cross the wire.
// owner=0 preserves the legacy single-system public test contract.
ClientProjection project(const Simulation& world, PlayerId owner = 0) {
    ClientProjection p;
    p.tick = world.tick();p.system_id = world.system_id();
    if(owner) {
        bool found=false;
        for(const auto& [id,e]:world.entities()) {
            (void)id;
            if(e.type==EntityType::Ship && e.owner_id==owner) {
                p.system_id=e.system_id;found=true;break;
            }
        }
        if(!found)throw std::invalid_argument("viewer owns no ship");
    }
    for(const auto& [id,e] : world.entities()) {
        if(e.system_id!=p.system_id)continue;
        p.entities.emplace(id,e);
        if(e.type==EntityType::Ship && e.dock_state!=DockState::InSpace)
            p.docks.emplace(id,DockTelemetry{e.dock_state,e.dock_station,e.transition_ticks});
        if(e.type==EntityType::Ship && e.jump_state!=ShipJumpState::Ready)
            p.jumps.emplace(id,JumpTelemetry{e.jump_state,e.pending_gate,e.jump_ticks});
    }
    for(const auto& [id,j]:world.active_mining())
        if(p.entities.count(id) && p.entities.count(j.target))p.mining.emplace(id,j);
    for(const auto& [key,h]:world.hangars())
        if(p.entities.count(key.first) && (!owner || key.second==owner))p.hangars.emplace(key,h);
    for(const auto& [id,m]:world.markets())
        if(p.entities.count(id))p.markets.emplace(id,m);
    for(const auto& [id,credits]:world.wallets())
        if(!owner || id==owner)p.wallets.emplace(id,credits);
    return p;
}
std::uint64_t fnv(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : value) { hash ^= ch; hash *= 1099511628211ULL; }
    return hash;
}
void validate_projection(const ClientProjection& p) {
    if (p.system_id == 0 || p.entities.size() > 100000 || p.mining.size() > 100000)
        throw std::runtime_error("invalid projection metadata");
    for (const auto& [id,e] : p.entities) {
        if (id == 0 || id != e.id || e.system_id != p.system_id ||
            static_cast<unsigned>(e.type) > 3 || e.ore < 0 || e.cargo < 0 || e.cargo_capacity < 0 ||
            (e.type == EntityType::Ship && (e.owner_id == 0 || e.cargo_capacity == 0 || e.cargo > e.cargo_capacity || e.ore != 0)) ||
            (e.type == EntityType::Asteroid && (e.owner_id != 0 || e.cargo != 0 || e.cargo_capacity != 0)) ||
            ((e.type == EntityType::Station || e.type == EntityType::Stargate) &&
              ((e.type==EntityType::Stargate && e.owner_id!=0) || e.ore != 0 || e.cargo != 0 || e.cargo_capacity != 0)))
            throw std::runtime_error("invalid projected entity");
    }
    if(p.docks.size()>p.entities.size() || p.hangars.size()>100000)
        throw std::runtime_error("invalid station projection size");
    for(const auto& [id,d] : p.docks) {
        const auto ship=p.entities.find(id), station=p.entities.find(d.station);
        if(ship==p.entities.end() || ship->second.type!=EntityType::Ship ||
           station==p.entities.end() || station->second.type!=EntityType::Station ||
           d.state==DockState::InSpace || static_cast<unsigned>(d.state)>3 ||
           (d.state==DockState::Docked && d.remaining_ticks!=0) ||
           (d.state!=DockState::Docked && d.remaining_ticks==0))
            throw std::runtime_error("invalid dock state projection");
    }
    for(const auto& [id,j] : p.jumps) {
        auto it=p.entities.find(id);
        if(it==p.entities.end() || it->second.type!=EntityType::Ship ||
           j.state==ShipJumpState::Ready || static_cast<unsigned>(j.state)>2 ||
           j.remaining_ticks==0 ||
           (j.state==ShipJumpState::Jumping &&
              (j.gate==0 || !p.entities.count(j.gate) || p.entities.at(j.gate).type!=EntityType::Stargate)) ||
           (j.state==ShipJumpState::ArrivalCloak && j.gate!=0))
            throw std::runtime_error("invalid jump telemetry");
    }
    for(const auto& [key,h] : p.hangars) {
        const auto station=p.entities.find(key.first);
        if(station==p.entities.end() || station->second.type!=EntityType::Station ||
           key.second==0 || h.capacity<=0 || h.ore<0 || h.ore>h.capacity)
            throw std::runtime_error("invalid hangar projection");
    }
    for(const auto& [owner, credits] : p.wallets) {
        if(owner==0 || credits<0) throw std::runtime_error("invalid wallet projection");
    }
    for(const auto& [id, m] : p.markets) {
        auto e=p.entities.find(id);
        if(e==p.entities.end() || e->second.type!=EntityType::Station ||
           m.ore<0 || m.credits<0 || m.price_per_ore<=0)
            throw std::runtime_error("invalid market projection");
    }
    for (const auto& [id,job] : p.mining) {
        const auto ship = p.entities.find(id);
        const auto rock = p.entities.find(job.target);
        if (id == 0 || job.target == 0 || job.amount_per_tick <= 0 || job.start_command_id == 0 ||
            ship == p.entities.end() || ship->second.type != EntityType::Ship ||
            rock == p.entities.end() || rock->second.type != EntityType::Asteroid)
            throw std::runtime_error("invalid projected mining operation");
    }
}
void push_upsert(std::vector<Change>& changes, const Entity& e) {
    Change c; c.kind = ChangeKind::Upsert; c.entity = e; changes.push_back(c);
}
void push_job(std::vector<Change>& changes, EntityId id, const AutoMining& j) {
    Change c; c.kind = ChangeKind::MiningUpsert; c.id = id; c.mining = j; changes.push_back(c);
}
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> parts;
    std::size_t start=0;
    for (std::size_t i=0; i<=s.size(); ++i) {
        if (i == s.size() || s[i] == '|') {
            parts.push_back(s.substr(start,i-start)); start=i+1;
            if(parts.size()>20) throw std::runtime_error("too many fields");
        }
    }
    return parts;
}
template <typename T> T number(const std::string& v) {
    static_assert(std::is_integral<T>::value, "integral only");
    if (v.empty() || v[0]=='+' || v.size()>22) throw std::runtime_error("bad number");
    T x{};
    const auto result=std::from_chars(v.data(),v.data()+v.size(),x);
    if(result.ec != std::errc{} || result.ptr != v.data()+v.size()) throw std::runtime_error("bad number");
    return x;
}
void require(const std::vector<std::string>& f, std::size_t n) {
    if(f.size()!=n) throw std::runtime_error("wrong field count");
}
Entity parse_entity(const std::vector<std::string>& f) {
    require(f,12);
    Entity e;
    e.id=number<std::uint64_t>(f[1]);
    e.system_id=number<std::uint32_t>(f[2]);
    const auto type=number<std::uint32_t>(f[3]);
    if(type>3) throw std::runtime_error("invalid entity type");
    e.type=static_cast<EntityType>(type); e.owner_id=number<std::uint64_t>(f[4]);
    e.position.x=number<std::int64_t>(f[5]); e.position.y=number<std::int64_t>(f[6]);
    e.velocity.x=number<std::int64_t>(f[7]); e.velocity.y=number<std::int64_t>(f[8]);
    e.ore=number<std::int64_t>(f[9]); e.cargo=number<std::int64_t>(f[10]);
    e.cargo_capacity=number<std::int64_t>(f[11]);
    return e;
}
void print_entity(std::ostream& out, const Entity& e) {
    out << '|' << e.id << '|' << e.system_id << '|' << static_cast<unsigned>(e.type)
        << '|' << e.owner_id << '|' << e.position.x << '|' << e.position.y
        << '|' << e.velocity.x << '|' << e.velocity.y << '|' << e.ore
        << '|' << e.cargo << '|' << e.cargo_capacity;
}
}

std::uint64_t projection_hash(const ClientProjection& p) {
    validate_projection(p);
    std::ostringstream out;
    out << p.system_id << '|' << p.tick << '\n';
    for (const auto& [id,e] : p.entities) {
        (void)id;
        out << 'E'; print_entity(out,e); out << '\n';
    }
    for (const auto& [id,j] : p.mining) {
        out << "J|" << id << '|' << j.target << '|' << j.amount_per_tick << '|' << j.start_command_id << '\n';
    }
    // Optional B5.2 extension: unchanged hashes for legacy B3 frames with no station state.
    for(const auto& [id,d] : p.docks)
        out << "Q|" << id << '|' << static_cast<unsigned>(d.state) << '|'
            << d.station << '|' << d.remaining_ticks << '\n';
    for(const auto& [id,j] : p.jumps)
        out << "K|" << id << '|' << static_cast<unsigned>(j.state) << '|'
            << j.gate << '|' << j.remaining_ticks << '\n';
    for(const auto& [key,h] : p.hangars)
        out << "H|" << key.first << '|' << key.second << '|'
            << h.ore << '|' << h.capacity << '\n';
    for(const auto& [owner,credits] : p.wallets)
        out << "W|" << owner << '|' << credits << '\n';
    for(const auto& [id,m] : p.markets)
        out << "M|" << id << '|' << m.ore << '|' << m.credits << '|' << m.price_per_ore << '\n';
    return fnv(out.str());
}

ServerViewBridge::ServerViewBridge(const Simulation& world, std::uint64_t stream_id, PlayerId viewer_owner)
    : previous_(project(world,viewer_owner)), viewer_owner_(viewer_owner), stream_id_(stream_id),
      command_cursor_(world.events().size()), activity_cursor_(world.activity_events().size()) {
    if(stream_id==0) throw std::invalid_argument("stream_id must be nonzero");
    (void)projection_hash(previous_);
}
ViewFrame ServerViewBridge::snapshot() const {
    ViewFrame f;
    f.kind=FrameKind::Snapshot; f.stream_id=stream_id_; f.sequence=sequence_;
    f.system_id=previous_.system_id; f.tick=previous_.tick;
    for (const auto& [id,e] : previous_.entities) { (void)id; push_upsert(f.changes,e); }
    for (const auto& [id,j] : previous_.mining) push_job(f.changes,id,j);
    for(const auto& [id,d] : previous_.docks) {
        Change c; c.kind=ChangeKind::DockUpsert; c.id=id; c.dock=d; f.changes.push_back(c);
    }
    for(const auto& [id,j]:previous_.jumps) {
        Change c;c.kind=ChangeKind::JumpUpsert;c.id=id;c.jump=j;f.changes.push_back(c);
    }
    for(const auto& [key,h] : previous_.hangars) {
        Change c; c.kind=ChangeKind::HangarUpsert; c.station_id=key.first;
        c.owner_id=key.second; c.hangar=h; f.changes.push_back(c);
    }
    for(const auto& [owner,credits] : previous_.wallets) {
        Change c; c.kind=ChangeKind::WalletUpsert; c.owner_id=owner; c.credits=credits; f.changes.push_back(c);
    }
    for(const auto& [id,m] : previous_.markets) {
        Change c; c.kind=ChangeKind::MarketUpsert; c.station_id=id; c.market=m; f.changes.push_back(c);
    }
    f.projection_hash=projection_hash(previous_);
    return f;
}
ViewFrame ServerViewBridge::after_committed_step(const Simulation& world) {
    if(world.tick()!=previous_.tick+1 ||
       world.events().size()<command_cursor_ || world.activity_events().size()<activity_cursor_ ||
       sequence_==std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("observation must follow one committed tick");
    ClientProjection now=project(world,viewer_owner_);
    // System transfer is a complete authority/view change. Emit a new full
    // snapshot at the same stream epoch, with strictly increasing sequence.
    if(now.system_id!=previous_.system_id) {
        (void)projection_hash(now);
        previous_=std::move(now);
        command_cursor_=world.events().size();
        activity_cursor_=world.activity_events().size();
        ++sequence_;
        return snapshot();
    }
    ViewFrame f;
    f.kind=FrameKind::Delta; f.stream_id=stream_id_; f.sequence=sequence_+1;
    f.system_id=now.system_id; f.tick=now.tick;
    for (const auto& [id,e] : previous_.entities) {
        (void)e;
        if(!now.entities.count(id)) { Change c; c.kind=ChangeKind::Remove; c.id=id; f.changes.push_back(c); }
    }
    for (const auto& [id,e] : now.entities) {
        auto it=previous_.entities.find(id);
        if(it==previous_.entities.end() || !same(it->second,e)) push_upsert(f.changes,e);
    }
    for (const auto& [id,j] : previous_.mining) {
        (void)j;
        if(!now.mining.count(id)) { Change c; c.kind=ChangeKind::MiningRemove; c.id=id; f.changes.push_back(c); }
    }
    for (const auto& [id,j] : now.mining) {
        auto it=previous_.mining.find(id);
        if(it==previous_.mining.end() || !same(it->second,j)) push_job(f.changes,id,j);
    }
    for(const auto& [id,d] : previous_.docks) {
        (void)d;
        if(!now.docks.count(id)) {
            Change c; c.kind=ChangeKind::DockRemove; c.id=id; f.changes.push_back(c);
        }
    }
    for(const auto& [id,d] : now.docks) {
        auto old=previous_.docks.find(id);
        if(old==previous_.docks.end() || !same(old->second,d)) {
            Change c; c.kind=ChangeKind::DockUpsert; c.id=id; c.dock=d; f.changes.push_back(c);
        }
    }
    for(const auto& [id,j]:previous_.jumps) {
        (void)j;
        if(!now.jumps.count(id)) {
            Change c;c.kind=ChangeKind::JumpRemove;c.id=id;f.changes.push_back(c);
        }
    }
    for(const auto& [id,j]:now.jumps) {
        auto old=previous_.jumps.find(id);
        if(old==previous_.jumps.end() || !same(old->second,j)) {
            Change c;c.kind=ChangeKind::JumpUpsert;c.id=id;c.jump=j;f.changes.push_back(c);
        }
    }
    for(const auto& [key,h] : previous_.hangars) {
        (void)h;
        if(!now.hangars.count(key)) {
            Change c; c.kind=ChangeKind::HangarRemove;
            c.station_id=key.first; c.owner_id=key.second; f.changes.push_back(c);
        }
    }
    for(const auto& [key,h] : now.hangars) {
        auto old=previous_.hangars.find(key);
        if(old==previous_.hangars.end() || !same(old->second,h)) {
            Change c; c.kind=ChangeKind::HangarUpsert; c.station_id=key.first;
            c.owner_id=key.second; c.hangar=h; f.changes.push_back(c);
        }
    }
    for(const auto& [owner, credits] : previous_.wallets) {
        (void)credits;
        if(!now.wallets.count(owner)) {
            Change c; c.kind=ChangeKind::WalletRemove; c.owner_id=owner; f.changes.push_back(c);
        }
    }
    for(const auto& [owner,credits] : now.wallets) {
        auto old=previous_.wallets.find(owner);
        if(old==previous_.wallets.end() || old->second!=credits) {
            Change c; c.kind=ChangeKind::WalletUpsert; c.owner_id=owner; c.credits=credits; f.changes.push_back(c);
        }
    }
    for(const auto& [id,m] : previous_.markets) {
        (void)m;
        if(!now.markets.count(id)) {
            Change c; c.kind=ChangeKind::MarketRemove; c.station_id=id; f.changes.push_back(c);
        }
    }
    for(const auto& [id,m] : now.markets) {
        auto old=previous_.markets.find(id);
        if(old==previous_.markets.end() || !same(old->second,m)) {
            Change c; c.kind=ChangeKind::MarketUpsert; c.station_id=id; c.market=m; f.changes.push_back(c);
        }
    }
    for(std::size_t i=command_cursor_;i<world.events().size();++i) {
        Change c; c.kind=ChangeKind::CommandResult; c.command=world.events()[i]; f.changes.push_back(c);
    }
    for(std::size_t i=activity_cursor_;i<world.activity_events().size();++i) {
        Change c; c.kind=ChangeKind::Activity; c.activity=world.activity_events()[i]; f.changes.push_back(c);
    }
    f.projection_hash=projection_hash(now);
    // Do not advance bridge state if unable to construct a valid bounded frame.
    (void)encode_frame(f);
    previous_=std::move(now); command_cursor_=world.events().size();
    activity_cursor_=world.activity_events().size(); ++sequence_;
    return f;
}

ApplyResult ClientView::apply(const ViewFrame& f) {
    if(f.stream_id==0 || f.system_id==0 || f.changes.size()>kMaxChanges)
        throw std::runtime_error("invalid frame header");
    if(f.kind==FrameKind::Delta) {
        if(!initialized_ || f.stream_id!=stream_id_ || f.system_id!=current_.system_id)
            return ApplyResult::NeedsSnapshot;
        if(f.sequence==sequence_ && f.tick==current_.tick) {
            if(f.projection_hash!=projection_hash(current_)) throw std::runtime_error("conflicting duplicate");
            return ApplyResult::DuplicateIgnored;
        }
        if(sequence_==std::numeric_limits<std::uint64_t>::max() ||
           current_.tick==std::numeric_limits<std::uint64_t>::max() ||
           f.sequence!=sequence_+1 || f.tick!=current_.tick+1)
            return ApplyResult::NeedsSnapshot;
    } else if(f.kind==FrameKind::Snapshot) {
        if(initialized_ && f.stream_id==stream_id_) {
            if(f.sequence<sequence_) return ApplyResult::NeedsSnapshot;
            if(f.sequence==sequence_ && f.tick==current_.tick) {
                if(f.projection_hash!=projection_hash(current_)) throw std::runtime_error("conflicting snapshot");
                return ApplyResult::DuplicateIgnored;
            }
        }
    } else throw std::runtime_error("invalid frame kind");
    ClientProjection next = f.kind==FrameKind::Snapshot ? ClientProjection{} : current_;
    next.system_id=f.system_id; next.tick=f.tick;
    std::vector<Change> notifications;
    for(const Change& c : f.changes) {
        switch(c.kind) {
            case ChangeKind::Upsert:
                if(c.entity.id==0 || c.entity.system_id!=f.system_id ||
                   (f.kind==FrameKind::Snapshot && next.entities.count(c.entity.id)))
                    throw std::runtime_error("bad upsert");
                next.entities[c.entity.id]=c.entity; break;
            case ChangeKind::Remove:
                if(f.kind==FrameKind::Snapshot || !next.entities.erase(c.id))
                    throw std::runtime_error("bad remove");
                break;
            case ChangeKind::MiningUpsert:
                if(c.id==0 || (f.kind==FrameKind::Snapshot && next.mining.count(c.id)))
                    throw std::runtime_error("bad mining upsert");
                next.mining[c.id]=c.mining; break;
            case ChangeKind::MiningRemove:
                if(f.kind==FrameKind::Snapshot || !next.mining.erase(c.id))
                    throw std::runtime_error("bad mining remove");
                break;
            case ChangeKind::DockUpsert:
                if(c.id==0 || (f.kind==FrameKind::Snapshot && next.docks.count(c.id)))
                    throw std::runtime_error("bad dock upsert");
                next.docks[c.id]=c.dock; break;
            case ChangeKind::DockRemove:
                if(f.kind==FrameKind::Snapshot || !next.docks.erase(c.id))
                    throw std::runtime_error("bad dock remove");
                break;
            case ChangeKind::JumpUpsert:
                if(c.id==0 || (f.kind==FrameKind::Snapshot && next.jumps.count(c.id)))
                    throw std::runtime_error("bad jump upsert");
                next.jumps[c.id]=c.jump;break;
            case ChangeKind::JumpRemove:
                if(f.kind==FrameKind::Snapshot || !next.jumps.erase(c.id))
                    throw std::runtime_error("bad jump removal");
                break;
            case ChangeKind::HangarUpsert:
                if(c.station_id==0 || c.owner_id==0 ||
                   (f.kind==FrameKind::Snapshot && next.hangars.count({c.station_id,c.owner_id})))
                    throw std::runtime_error("bad hangar upsert");
                next.hangars[{c.station_id,c.owner_id}]=c.hangar; break;
            case ChangeKind::HangarRemove:
                if(f.kind==FrameKind::Snapshot || !next.hangars.erase({c.station_id,c.owner_id}))
                    throw std::runtime_error("bad hangar remove");
                break;
            case ChangeKind::WalletUpsert:
                if(c.owner_id==0 || c.credits<0 ||
                   (f.kind==FrameKind::Snapshot && next.wallets.count(c.owner_id)))
                    throw std::runtime_error("bad wallet upsert");
                next.wallets[c.owner_id]=c.credits;break;
            case ChangeKind::WalletRemove:
                if(f.kind==FrameKind::Snapshot || !next.wallets.erase(c.owner_id))
                    throw std::runtime_error("bad wallet removal");
                break;
            case ChangeKind::MarketUpsert:
                if(c.station_id==0 || c.market.ore<0 || c.market.credits<0 || c.market.price_per_ore<=0 ||
                   (f.kind==FrameKind::Snapshot && next.markets.count(c.station_id)))
                    throw std::runtime_error("bad market upsert");
                next.markets[c.station_id]=c.market;break;
            case ChangeKind::MarketRemove:
                if(f.kind==FrameKind::Snapshot || !next.markets.erase(c.station_id))
                    throw std::runtime_error("bad market removal");
                break;
            case ChangeKind::CommandResult:
                if(f.kind==FrameKind::Snapshot || c.command.tick!=f.tick ||
                   c.command.command_id==0) throw std::runtime_error("bad command event");
                notifications.push_back(c); break;
            case ChangeKind::Activity:
                if(f.kind==FrameKind::Snapshot || c.activity.tick!=f.tick ||
                   c.activity.target==0) throw std::runtime_error("bad activity event");
                notifications.push_back(c); break;
            default: throw std::runtime_error("unknown change kind");
        }
    }
    if(projection_hash(next)!=f.projection_hash) throw std::runtime_error("projection mismatch");
    current_=std::move(next);
    notifications_=std::move(notifications);
    stream_id_=f.stream_id; sequence_=f.sequence; initialized_=true;
    return ApplyResult::Applied;
}

std::string encode_frame(const ViewFrame& f) {
    if(f.stream_id==0 || f.system_id==0 || f.changes.size()>kMaxChanges ||
       (f.kind!=FrameKind::Snapshot && f.kind!=FrameKind::Delta))
        throw std::runtime_error("invalid wire frame");
    std::ostringstream out;
    out << "SGVIEW|1|" << static_cast<unsigned>(f.kind) << '|' << f.stream_id << '|'
        << f.sequence << '|' << f.tick << '|' << f.system_id << '|'
        << f.projection_hash << '|' << f.changes.size() << '\n';
    for(const Change& c : f.changes) {
        switch(c.kind) {
            case ChangeKind::Upsert: out << 'E'; print_entity(out,c.entity); break;
            case ChangeKind::Remove: out << "D|" << c.id; break;
            case ChangeKind::MiningUpsert:
                out << "J|" << c.id << '|' << c.mining.target << '|'
                    << c.mining.amount_per_tick << '|' << c.mining.start_command_id; break;
            case ChangeKind::MiningRemove: out << "X|" << c.id; break;
            case ChangeKind::CommandResult:
                out << "C|" << c.command.tick << '|' << c.command.command_id << '|'
                    << static_cast<unsigned>(c.command.result) << '|' << c.command.transferred; break;
            case ChangeKind::DockUpsert:
                out << "Q|" << c.id << '|' << static_cast<unsigned>(c.dock.state)
                    << '|' << c.dock.station << '|' << c.dock.remaining_ticks; break;
            case ChangeKind::DockRemove: out << "Z|" << c.id; break;
            case ChangeKind::JumpUpsert:
                out << "K|" << c.id << '|' << static_cast<unsigned>(c.jump.state)
                    << '|' << c.jump.gate << '|' << c.jump.remaining_ticks; break;
            case ChangeKind::JumpRemove:
                out << "L|" << c.id;break;
            case ChangeKind::HangarUpsert:
                out << "H|" << c.station_id << '|' << c.owner_id << '|'
                    << c.hangar.ore << '|' << c.hangar.capacity; break;
            case ChangeKind::HangarRemove:
                out << "V|" << c.station_id << '|' << c.owner_id; break;
            case ChangeKind::WalletUpsert:
                out << "W|" << c.owner_id << '|' << c.credits; break;
            case ChangeKind::WalletRemove:
                out << "Y|" << c.owner_id; break;
            case ChangeKind::MarketUpsert:
                out << "M|" << c.station_id << '|' << c.market.ore << '|'
                    << c.market.credits << '|' << c.market.price_per_ore; break;
            case ChangeKind::MarketRemove:
                out << "N|" << c.station_id; break;
            case ChangeKind::Activity:
                out << "A|" << c.activity.tick << '|' << static_cast<unsigned>(c.activity.kind)
                    << '|' << c.activity.start_command_id << '|' << c.activity.actor << '|'
                    << c.activity.target << '|' << static_cast<unsigned>(c.activity.result)
                    << '|' << c.activity.transferred; break;
            default: throw std::runtime_error("invalid change");
        }
        out << '\n';
        if(out.tellp()>static_cast<std::streamoff>(kMaxFrameBytes)) throw std::runtime_error("frame too large");
    }
    out << "END\n";
    const auto bytes=out.str();
    if(bytes.size()>kMaxFrameBytes) throw std::runtime_error("frame too large");
    return bytes;
}

ViewFrame decode_frame(const std::string& bytes) {
    if(bytes.empty() || bytes.size()>kMaxFrameBytes || bytes.back()!='\n' ||
       bytes.find('\r')!=std::string::npos || bytes.find('\0')!=std::string::npos)
        throw std::runtime_error("invalid wire framing");
    std::istringstream in(bytes);
    std::string line;
    if(!std::getline(in,line)) throw std::runtime_error("missing header");
    const auto h=split(line); require(h,9);
    if(h[0]!="SGVIEW" || h[1]!="1") throw std::runtime_error("unsupported frame version");
    ViewFrame f;
    const auto k=number<unsigned>(h[2]);
    if(k<1 || k>2) throw std::runtime_error("unknown frame kind");
    f.kind=static_cast<FrameKind>(k);
    f.stream_id=number<std::uint64_t>(h[3]); f.sequence=number<std::uint64_t>(h[4]);
    f.tick=number<std::uint64_t>(h[5]); f.system_id=number<std::uint32_t>(h[6]);
    f.projection_hash=number<std::uint64_t>(h[7]);
    const auto n=number<std::size_t>(h[8]);
    if(n>kMaxChanges) throw std::runtime_error("too many changes");
    for(std::size_t i=0;i<n;++i) {
        if(!std::getline(in,line)) throw std::runtime_error("truncated frame");
        const auto p=split(line);
        Change c;
        if(p[0]=="E") { c.kind=ChangeKind::Upsert; c.entity=parse_entity(p); }
        else if(p[0]=="D") { require(p,2); c.kind=ChangeKind::Remove; c.id=number<std::uint64_t>(p[1]); }
        else if(p[0]=="J") {
            require(p,5); c.kind=ChangeKind::MiningUpsert; c.id=number<std::uint64_t>(p[1]);
            c.mining.target=number<std::uint64_t>(p[2]); c.mining.amount_per_tick=number<std::int64_t>(p[3]);
            c.mining.start_command_id=number<std::uint64_t>(p[4]);
        }
        else if(p[0]=="X") { require(p,2); c.kind=ChangeKind::MiningRemove; c.id=number<std::uint64_t>(p[1]); }
        else if(p[0]=="Q") {
            require(p,5); c.kind=ChangeKind::DockUpsert;
            c.id=number<EntityId>(p[1]);
            const auto state=number<unsigned>(p[2]);
            if(state<1 || state>3) throw std::runtime_error("invalid dock state");
            c.dock.state=static_cast<DockState>(state);
            c.dock.station=number<EntityId>(p[3]);
            c.dock.remaining_ticks=number<std::uint32_t>(p[4]);
        }
        else if(p[0]=="Z") {require(p,2); c.kind=ChangeKind::DockRemove; c.id=number<EntityId>(p[1]);}
        else if(p[0]=="K") {
            require(p,5);c.kind=ChangeKind::JumpUpsert;c.id=number<EntityId>(p[1]);
            const auto state=number<unsigned>(p[2]);
            if(state<1 || state>2)throw std::runtime_error("invalid jump state");
            c.jump.state=static_cast<ShipJumpState>(state);
            c.jump.gate=number<EntityId>(p[3]);
            c.jump.remaining_ticks=number<std::uint32_t>(p[4]);
        }
        else if(p[0]=="L") {require(p,2);c.kind=ChangeKind::JumpRemove;c.id=number<EntityId>(p[1]);}
        else if(p[0]=="H") {
            require(p,5); c.kind=ChangeKind::HangarUpsert;
            c.station_id=number<EntityId>(p[1]); c.owner_id=number<PlayerId>(p[2]);
            c.hangar.ore=number<std::int64_t>(p[3]); c.hangar.capacity=number<std::int64_t>(p[4]);
        }
        else if(p[0]=="V") {
            require(p,3); c.kind=ChangeKind::HangarRemove;
            c.station_id=number<EntityId>(p[1]); c.owner_id=number<PlayerId>(p[2]);
        }
        else if(p[0]=="W") {
            require(p,3);c.kind=ChangeKind::WalletUpsert;
            c.owner_id=number<PlayerId>(p[1]); c.credits=number<std::int64_t>(p[2]);
        }
        else if(p[0]=="Y") {
            require(p,2);c.kind=ChangeKind::WalletRemove;c.owner_id=number<PlayerId>(p[1]);
        }
        else if(p[0]=="M") {
            require(p,5);c.kind=ChangeKind::MarketUpsert;
            c.station_id=number<EntityId>(p[1]);c.market.ore=number<std::int64_t>(p[2]);
            c.market.credits=number<std::int64_t>(p[3]);c.market.price_per_ore=number<std::int64_t>(p[4]);
        }
        else if(p[0]=="N") {
            require(p,2);c.kind=ChangeKind::MarketRemove;c.station_id=number<EntityId>(p[1]);
        }
        else if(p[0]=="C") {
            require(p,5); c.kind=ChangeKind::CommandResult;
            c.command.tick=number<std::uint64_t>(p[1]); c.command.command_id=number<std::uint64_t>(p[2]);
            const auto outcome=number<unsigned>(p[3]);
            if(outcome>static_cast<unsigned>(Outcome::GateOffline)) throw std::runtime_error("invalid outcome");
            c.command.result=static_cast<Outcome>(outcome);
            c.command.transferred=number<std::int64_t>(p[4]);
        }
        else if(p[0]=="A") {
            require(p,8); c.kind=ChangeKind::Activity;
            c.activity.tick=number<std::uint64_t>(p[1]); const auto kind=number<unsigned>(p[2]);
            if(kind>static_cast<unsigned>(ActivityKind::JumpCompleted)) throw std::runtime_error("invalid activity kind");
            c.activity.kind=static_cast<ActivityKind>(kind);
            c.activity.start_command_id=number<std::uint64_t>(p[3]);
            c.activity.actor=number<std::uint64_t>(p[4]); c.activity.target=number<std::uint64_t>(p[5]);
            const auto outcome=number<unsigned>(p[6]);
            if(outcome>static_cast<unsigned>(Outcome::GateOffline)) throw std::runtime_error("invalid outcome");
            c.activity.result=static_cast<Outcome>(outcome);
            c.activity.transferred=number<std::int64_t>(p[7]);
        } else throw std::runtime_error("unknown wire change");
        f.changes.push_back(c);
    }
    if(!std::getline(in,line) || line!="END" || std::getline(in,line))
        throw std::runtime_error("wrong end marker/trailing payload");
    // Re-encoding also rejects noncanonical numbers and malformed separators.
    if(encode_frame(f)!=bytes) throw std::runtime_error("noncanonical frame encoding");
    return f;
}
} // namespace galaxy
