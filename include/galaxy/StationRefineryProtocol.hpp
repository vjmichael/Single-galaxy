#pragma once
// PLAY-STATION-03C: read-only wire projection of the ORIGINAL committed world.
// This code has NO mutation authority. Actor/lot validation and SQLite writes
// are performed only via PersistentWorld::submit/step by the native host.
#include "galaxy/Persistence.hpp"
#include "galaxy/StationMineralIdentityAudit.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace galaxy::station_refinery_protocol {
static constexpr std::int64_t kTerminalXmm=3000;
static constexpr std::int64_t kTerminalZmm=0;
// Pictured 0.9m crate plus native 340mm capsule require a usable approach
// beyond the former 700mm, otherwise the real terminal is unreachable.
static constexpr std::int64_t kTerminalRadiusMm=1200;
static constexpr std::size_t kMaxVisibleLots=48;

// A colony operations interface is a local colony console, not a second
// inventory or permission to access a distant station. Remote here means
// anywhere INSIDE the player's currently verified physical colony.
inline bool colony_resident_access(const PlayerPerson& person) noexcept {
    return person.state==PlayerPersonState::AtStation &&
           person.owner!=0 && person.physical_station!=0;
}

inline bool accessible(const PlayerPerson& person) noexcept {
    if(person.state!=PlayerPersonState::AtStation || !person.physical_station)
        return false;
    const auto dx=person.station_x_mm-kTerminalXmm;
    const auto dz=person.station_z_mm-kTerminalZmm;
    return dx>=-kTerminalRadiusMm && dx<=kTerminalRadiusMm &&
           dz>=-kTerminalRadiusMm && dz<=kTerminalRadiusMm &&
           dx*dx+dz*dz<=kTerminalRadiusMm*kTerminalRadiusMm;
}

// All frames are staged first and only then sent to the client. A partial
// source read never changes stock; no client-provided mineral quantities.
inline std::string make_snapshot(const PersistentWorld& world,
                                 PlayerId owner, std::uint64_t session,
                                 std::uint64_t request, int result,
                                 std::int64_t transferred,
                                 bool colony_console=false) {
    const auto pit=world.world().player_people().find(owner);
    if(pit==world.world().player_people().end() ||
       pit->second.owner!=owner ||
       !(colony_console?colony_resident_access(pit->second):
                           accessible(pit->second)))
        throw std::invalid_argument("refinery terminal requires station resident");
    const auto station=pit->second.physical_station;
    const auto* site=world.world().find(station);
    if(!site || site->type!=EntityType::Station)
        throw std::runtime_error("refinery terminal station missing");
    const auto* plant=world.world().station_refinery(station);
    const auto* utility=world.world().utility(station);
    const auto* hangar=world.world().hangar(station,owner);
    const bool present=plant && plant->owner==owner;
    const std::int64_t energy=utility?utility->stored_energy:0;
    const bool powered=utility&&utility->powered && utility->condition>0;
    std::int64_t mineral_total=0;
    std::size_t mineral_count=0;
    if(present)for(const auto& [name,kg]:plant->mineral_kg){
        if(kg<0 || kg>1000000000LL || name.empty() || name.size()>40 ||
           name.find('|')!=std::string::npos || name.find('\n')!=std::string::npos ||
           kg>std::numeric_limits<std::int64_t>::max()-mineral_total)
            throw std::runtime_error("invalid original station mineral stock");
        mineral_total+=kg;
        if(kg)++mineral_count;
    }
    if(present){
        // Validate the actual stored concentrate identity and kilogram unit
        // against the pre-existing original INDUSTRY-01 legacy registry.
        // This is a *read-only* audit: NO candidate item conversion, no
        // phantom workshop materials and no second inventory.
        const auto audit=station_material_identity::audit_original_station(
            world.world(),station,owner);
        if(!audit || audit->stock_kilograms!=mineral_total ||
           audit->stock.size()!=mineral_count)
            throw std::runtime_error("unrecognized original station mineral identity");
    }
    std::size_t visible=0, all=0;
    for(const auto& [id,lot]:world.world().station_ore_lots()){
        (void)id;
        if(lot.station==station&&lot.owner==owner&&lot.tonnes>0&&lot.taxonomy<=2){
            ++all;
            if(visible<kMaxVisibleLots)++visible;
        }
    }
    std::ostringstream out;
    // Header count/ack is verified by the UI before it displays any state.
    out<<"REFHEAD|3|"<<session<<'|'<<request<<'|'<<world.world().tick()
       <<'|'<<world.generation()<<'|'<<station<<'|'<<(present?1:0)
       <<'|'<<(present?plant->source_ship:0)<<'|'
       <<(present?plant->assigned_workers:0)<<'|'<<(powered?1:0)
       <<'|'<<energy<<'|'<<(present?plant->ready_tick:0)
       <<'|'<<(hangar?hangar->ore:0)<<'|'<<mineral_total
       <<'|'<<visible<<'|'<<all<<'|'<<mineral_count<<'\n';
    std::size_t shown=0;
    for(const auto& [id,lot]:world.world().station_ore_lots()){
        if(lot.station!=station || lot.owner!=owner || lot.tonnes<1 || lot.taxonomy>2)continue;
        if(shown++==kMaxVisibleLots)break;
        out<<"REFLOT|3|"<<session<<'|'<<request<<'|'<<id<<'|'
           <<lot.rock<<'|'<<unsigned(lot.taxonomy)<<'|'<<lot.tonnes
           <<'|'<<lot.unload_receipt<<'\n';
    }
    if(present)for(const auto& [name,kg]:plant->mineral_kg)
        if(kg>0)out<<"REFMIN|3|"<<session<<'|'<<request<<'|'<<name<<'|'<<kg<<'\n';
    out<<"REFEND|3|"<<session<<'|'<<request<<'|'<<world.world().tick()
       <<'|'<<world.generation()<<'|'<<visible<<'|'<<mineral_count
       <<'|'<<result<<'|'<<transferred<<'\n';
    return out.str();
}
} // namespace galaxy::station_refinery_protocol
