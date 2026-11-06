#pragma once
// SHIP-STATUS-01: deterministic, bounded temporary equipment effects.
// This is a policy kernel, not a parallel ship entity, hacking transport,
// inventory, or grant of combat authority. Host validates real attacks.
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::ship_status {

enum class Kind : std::uint8_t { Confusion=1 };
// Identity is (rack,slot,hardware type), NOT a free-floating client module.
struct ModuleKey {
    std::uint8_t rack=0, slot=0, hardware=0;
    bool operator<(const ModuleKey& b) const noexcept {
        if(rack!=b.rack)return rack<b.rack;
        if(slot!=b.slot)return slot<b.slot;
        return hardware<b.hardware;
    }
    bool operator==(const ModuleKey& b) const noexcept {
        return rack==b.rack && slot==b.slot && hardware==b.hardware;
    }
};
struct ModuleControl {
    ModuleKey key{};
    bool requested_on=false; // original pilot/NPC intent, never overwritten
    bool network_exposed=false; // host-verified attack surface
    bool isolated=false; // current defensive isolation
};
struct ModuleState {
    ModuleKey key{};
    bool requested_on=false,effective_on=false,confused=false;
};
struct Effect {
    std::uint64_t id=0,source_ship=0,target_ship=0;
    std::uint64_t started_tick=0,expires_tick=0,seed=0;
    std::uint32_t pulse_ticks=0;
    std::uint8_t intensity=0,resistance=0; // 1-100 / 0-100
    Kind kind=Kind::Confusion;
    std::vector<ModuleKey> targets; // immutable original fitted attack surface
};

class Engine {
public:
    static constexpr std::uint64_t kMaxDurationTicks=300;
    static constexpr std::size_t kMaxAffectedModules=16;
    static constexpr std::size_t kMaxRecords=2048;
    // Trusted host only: source/target, attack delivery and authorization
    // must be proven against the real world before calling this function.
    // Countermeasures subtract intensity; a resisted attack is not recorded.
    bool inflict_confusion(Effect proposed,
                          const std::vector<ModuleControl>& fitted,
                          std::uint64_t current_tick) {
        if(proposed.kind!=Kind::Confusion ||
           proposed.started_tick!=current_tick ||
           !valid_header(proposed) || !proposed.targets.empty() ||
           proposed.intensity<=proposed.resistance ||
           effects_.count(proposed.id) || effects_.size()>=kMaxRecords ||
           overlaps(proposed))return false;
        if(!unique_fitting(fitted))return false;
        for(const auto& module:fitted) {
            if(module.network_exposed && !module.isolated &&
               module.key.hardware!=0)
                proposed.targets.push_back(module.key);
        }
        std::sort(proposed.targets.begin(),proposed.targets.end());
        if(proposed.targets.empty() ||
           proposed.targets.size()>kMaxAffectedModules)return false;
        effects_.emplace(proposed.id,std::move(proposed));
        return true;
    }
    // A read projection, NOT a client-side toggle. The authoritative module
    // executor must consume effective_on at command execution (not only UI).
    // No external randomness, so identical saved effects replay identically.
    std::vector<ModuleState> project(std::uint64_t ship,std::uint64_t tick,
                                     const std::vector<ModuleControl>& fitted) const {
        if(!unique_fitting(fitted))
            throw std::invalid_argument("ship status: duplicate installed module");
        std::map<ModuleKey,bool> inverted;
        for(const auto& [id,e]:effects_) {
            (void)id;
            if(e.target_ship!=ship ||
               tick<e.started_tick || tick>=e.expires_tick)continue;
            const auto last=(tick-e.started_tick)/e.pulse_ticks;
            const auto strength=unsigned(e.intensity)-unsigned(e.resistance);
            const auto limit=std::min<std::size_t>(e.targets.size(),
                std::max<std::size_t>(1,(e.targets.size()*strength+99)/100));
            for(std::uint64_t cycle=0;cycle<=last;++cycle) {
                const auto salt=mix(e.seed ^ (cycle+1)*0x9e3779b97f4a7c15ULL);
                const auto count=1+static_cast<std::size_t>(salt%limit);
                std::vector<std::pair<std::uint64_t,ModuleKey>> order;
                for(const auto& target:e.targets) {
                    const auto packed=(std::uint64_t(target.rack)<<16) |
                        (std::uint64_t(target.slot)<<8) | target.hardware;
                    order.emplace_back(mix(salt^packed*0xbf58476d1ce4e5b9ULL),
                                       target);
                }
                std::sort(order.begin(),order.end(),
                    [](const auto& a,const auto& b){
                        return a.first!=b.first?a.first<b.first:a.second<b.second;
                    });
                for(std::size_t j=0;j<count;++j)
                    inverted[order[j].second]=!inverted[order[j].second];
            }
        }
        std::vector<ModuleState> view;
        view.reserve(fitted.size());
        for(const auto& module:fitted) {
            const bool changed=module.network_exposed && !module.isolated &&
                inverted[module.key];
            view.push_back({module.key,module.requested_on,
                changed?!module.requested_on:module.requested_on,changed});
        }
        return view;
    }
    std::vector<Effect> records() const {
        std::vector<Effect> out;
        for(const auto& [id,e]:effects_){(void)id;out.push_back(e);}
        return out;
    }
    // Bounded deterministic snapshot fragment. The trusted host must include
    // this fragment in the SAME canonical world commit as ship changes.
    std::string encode() const {
        std::ostringstream out;
        out<<"ship-status-v1\n";
        for(const auto& [id,e]:effects_) {
            (void)id;
            out<<"C "<<e.id<<' '<<e.source_ship<<' '<<e.target_ship<<' '
               <<e.started_tick<<' '<<e.expires_tick<<' '<<e.seed<<' '
               <<e.pulse_ticks<<' '<<unsigned(e.intensity)<<' '
               <<unsigned(e.resistance)<<' '<<e.targets.size();
            for(const auto& key:e.targets)
                out<<' '<<unsigned(key.rack)<<' '<<unsigned(key.slot)
                   <<' '<<unsigned(key.hardware);
            out<<'\n';
        }
        return out.str();
    }
    static Engine decode(const std::string& saved) {
        std::istringstream in(saved);
        std::string line;
        if(!std::getline(in,line) || line!="ship-status-v1")
            throw std::invalid_argument("ship status: incompatible snapshot");
        Engine result;
        while(std::getline(in,line)) {
            if(line.empty())throw std::invalid_argument("ship status: empty row");
            std::istringstream row(line);
            char label=0;
            Effect e{};
            unsigned intensity=0,resistance=0;
            std::size_t count=0;
            if(!(row>>label>>e.id>>e.source_ship>>e.target_ship>>
                  e.started_tick>>e.expires_tick>>e.seed>>
                  e.pulse_ticks>>intensity>>resistance>>count) ||
               label!='C' || intensity>100 || resistance>100 ||
               count==0 || count>kMaxAffectedModules)
                throw std::invalid_argument("ship status: invalid row");
            e.intensity=static_cast<std::uint8_t>(intensity);
            e.resistance=static_cast<std::uint8_t>(resistance);
            for(std::size_t i=0;i<count;++i) {
                unsigned rack=0,slot=0,hardware=0;
                if(!(row>>rack>>slot>>hardware) ||
                   rack>255 || slot>255 || hardware==0 || hardware>255)
                    throw std::invalid_argument("ship status: invalid module");
                e.targets.push_back({static_cast<std::uint8_t>(rack),
                    static_cast<std::uint8_t>(slot),
                    static_cast<std::uint8_t>(hardware)});
            }
            row>>std::ws;
            if(!row.eof() || !valid_header(e) ||
               e.intensity<=e.resistance ||
               !std::is_sorted(e.targets.begin(),e.targets.end()) ||
               std::adjacent_find(e.targets.begin(),e.targets.end())!=e.targets.end() ||
               result.effects_.count(e.id) ||
               result.effects_.size()>=kMaxRecords || result.overlaps(e))
                throw std::invalid_argument("ship status: forged effect ancestry");
            result.effects_.emplace(e.id,std::move(e));
        }
        return result;
    }
private:
    std::map<std::uint64_t,Effect> effects_;
    static std::uint64_t mix(std::uint64_t x) noexcept {
        x+=0x9e3779b97f4a7c15ULL;
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;
        return x^(x>>31);
    }
    static bool valid_header(const Effect& e) noexcept {
        return e.id && e.source_ship && e.target_ship &&
            e.source_ship!=e.target_ship && e.seed && e.pulse_ticks>=1 &&
            e.pulse_ticks<=60 && e.intensity>=1 && e.intensity<=100 &&
            e.resistance<=100 && e.expires_tick>e.started_tick &&
            e.expires_tick-e.started_tick<=kMaxDurationTicks;
    }
    static bool unique_fitting(const std::vector<ModuleControl>& fitted) {
        if(fitted.size()>kMaxAffectedModules)return false;
        std::set<std::pair<std::uint8_t,std::uint8_t>> slots;
        for(const auto& module:fitted)
            if(!slots.emplace(module.key.rack,module.key.slot).second)
                return false;
        return true;
    }
    bool overlaps(const Effect& candidate) const {
        for(const auto& [id,e]:effects_) {
            (void)id;
            if(e.target_ship==candidate.target_ship &&
               e.started_tick<candidate.expires_tick &&
               candidate.started_tick<e.expires_tick)return true;
        }
        return false;
    }
};

} // namespace galaxy::ship_status
