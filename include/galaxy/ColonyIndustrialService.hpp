#pragma once
// DOCK-07: read-only projection of ORIGINAL colony-local physical industrial
// stores and native job boards. No inventory table, cross-colony aggregation,
// implied transfer, recipe invention or client-authored quantities.
#include "galaxy/Persistence.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include "galaxy/StationMineralIdentityAudit.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace galaxy::colony_industry {
inline EntityId local_source_ship(const Simulation& sim, EntityId station,
                                  PlayerId owner) {
    const auto* entity=sim.find(station);
    const auto outpost=sim.built_outposts().find(station);
    if(!owner || !entity || entity->type!=EntityType::Station ||
       outpost==sim.built_outposts().end())return 0;
    const auto ship=outpost->second.source_colony_ship;
    const auto* vessel=sim.find(ship);
    if(!ship || !vessel || vessel->type!=EntityType::Ship ||
       vessel->owner_id!=owner || !sim.colony_ship(ship))return 0;
    return ship;
}
inline bool local_resident(const Simulation& sim,PlayerId owner,
                           EntityId station) {
    const auto person=sim.player_people().find(owner);
    const auto* entity=sim.find(station);
    return owner && entity && entity->type==EntityType::Station &&
        person!=sim.player_people().end() && person->second.owner==owner &&
        person->second.state==PlayerPersonState::AtStation &&
        person->second.physical_station==station;
}
inline bool native_workshop_at(const Simulation& sim,EntityId station,
                               PlayerId owner) {
    const auto ship=local_source_ship(sim,station,owner);
    const auto* shop=sim.workshop(station);
    return ship && shop && shop->source_ship==ship;
}
inline bool native_storage_at(const Simulation& sim,EntityId station,
                              PlayerId owner) {
    const auto ship=local_source_ship(sim,station,owner);
    const auto* store=sim.storage(station);
    return ship && store && store->source_ship==ship;
}
inline bool safe_item_code(const std::string& name) {
    if(name.empty() || name.size()>60)return false;
    for(const char ch:name)
        if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||
             ch=='/'||ch=='_'))return false;
    return true;
}
inline std::string make_snapshot(const PersistentWorld& world,PlayerId owner,
                                 std::uint64_t session,std::uint64_t request,
                                 int result=0) {
    const auto& sim=world.world();
    const auto person=sim.player_people().find(owner);
    if(person==sim.player_people().end() ||
       !local_resident(sim,owner,person->second.physical_station))
        throw std::invalid_argument("industrial inventory requires local original colony");
    const auto station=person->second.physical_station;
    const auto ship=local_source_ship(sim,station,owner);
    const auto* shop=native_workshop_at(sim,station,owner)?
        sim.workshop(station):nullptr;
    const auto* store=native_storage_at(sim,station,owner)?
        sim.storage(station):nullptr;
    const auto* plant=sim.station_refinery(station);
    if(plant && plant->owner!=owner)plant=nullptr;
    const auto* hangar=sim.hangar(station,owner);
    const auto* utility=sim.utility(station);
    const bool powered=utility && utility->powered && utility->condition>0 &&
                       utility->stored_energy>0;
    // Every stock row is attached to an ORIGINAL physical source. Repeated
    // item codes in different source stores are not merged or minted.
    constexpr std::size_t kMaxStockRows=64;
    std::vector<std::string> rows;
    bool truncated=false;
    const auto add=[&](char kind,std::uint32_t bin,const std::string& code,
                       std::int64_t kg) {
        if(kg<0 || !safe_item_code(code) || kg>1000000000000LL)
            throw std::runtime_error("invalid original industrial stock");
        if(!kg)return;
        if(rows.size()==kMaxStockRows){truncated=true;return;}
        std::ostringstream out;
        out<<"ISTOCK|1|"<<session<<'|'<<request<<'|'<<kind<<'|'<<bin
           <<'|'<<code<<'|'<<kg<<'\n';
        rows.push_back(out.str());
    };
    // Physically present founding hull is a separate local source.
    // Its construction modules are discrete items, NOT kilograms; the UI
    // labels this one explicit module row as units instead of kg.
    const auto* vessel=ship?sim.find(ship):nullptr;
    if(vessel && vessel->dock_state==DockState::Docked &&
       vessel->dock_station==station){
        const auto* cargo=sim.colony_ship(ship);
        if(cargo){
            for(const auto& [name,kg]:cargo->goods_kg)
                add('H',0,"good/"+name,kg);
            add('C',0,"module/construction",cargo->modules);
        }
    }
    if(plant){
        // Match the existing original REFHEAD mineral-identity guard.
        // Candidate industrial inputs may not disguise a station concentrate.
        const auto audit=station_material_identity::audit_original_station(
            sim,station,owner);
        if(!audit || audit->stock.size()!=plant->mineral_kg.size())
            throw std::runtime_error("unrecognized original station mineral identity");
        for(const auto& row:audit->stock)
            add('R',0,row.original_code,row.kilograms);
    }
    if(shop){
        add('W',0,"good/aluminum",shop->aluminum_kg);
        add('W',0,"good/aluminum_panels",shop->panels_kg);
        add('W',0,"good/aluminum_frames",shop->frames_kg);
        add('W',0,"good/aluminum_scrap",shop->scrap_kg);
    }
    if(store){
        for(const auto& [code,kg]:store->goods_kg)add('S',0,code,kg);
        for(const auto& [bin,goods]:store->extra_bins){
            if(bin==0 || bin==std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("invalid original constructed storage bin");
            for(const auto& [code,kg]:goods)add('S',bin,code,kg);
        }
    }
    std::ostringstream out;
    out<<"IHEAD|1|"<<session<<'|'<<request<<'|'<<sim.tick()
       <<'|'<<world.generation()<<'|'<<station<<'|'<<(ship?1:0)
       <<'|'<<(plant?1:0)<<'|'<<(shop?1:0)<<'|'<<(store?1:0)
       <<'|'<<rows.size()<<'|'<<(truncated?1:0)
       <<'|'<<(store?store->network_escrow_kg:0)
       <<'|'<<(hangar?hangar->ore:0)<<'|'<<result<<'\n';
    for(const auto& row:rows)out<<row;
    if(shop){
        for(unsigned slot=0;slot<2;++slot){
            const auto& approval=shop->approved_jobs[slot];
            const auto achieved=slot?shop->finisher_batches:shop->completed_batches;
            const auto finished=approval.command_id?
                approval.achieved(achieved):0;
            const auto& shift=shop->shifts[slot];
            const auto input=slot?shop->panels_kg:shop->aluminum_kg;
            const auto reserved=slot?shop->reserved_panels_kg:shop->reserved_aluminum_kg;
            const auto output=slot?shop->frames_kg:shop->panels_kg;
            const auto worker=slot?shop->finisher_npc_id:shop->operator_npc_id;
            out<<"IJOB|1|"<<session<<'|'<<request<<'|'<<(slot+1)
               <<'|'<<(approval.command_id?1:0)
               <<'|'<<approval.target_batches<<'|'<<finished
               <<'|'<<worker<<'|'<<shop->supervisor_npc_id
               <<'|'<<input<<'|'<<reserved<<'|'<<output
               <<'|'<<shop->scrap_kg<<'|'<<(powered?1:0)
               <<'|'<<(shift.in_effect(sim.tick())?1:0)<<'\n';
        }
    }
    out<<"IEND|1|"<<session<<'|'<<request<<'|'<<sim.tick()
       <<'|'<<world.generation()<<'|'<<rows.size()
       <<'|'<<(shop?2:0)<<'|'<<result<<'\n';
    return out.str();
}
} // namespace galaxy::colony_industry
