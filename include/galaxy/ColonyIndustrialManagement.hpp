#pragma once
// DOCK-08: local colony industrial facilities + finite named workforce.
// Read/proposal layer over ORIGINAL Simulation/PersistentWorld only.
// No parallel people, factory state, inventory balances or authority.
#include "galaxy/ColonyIndustrialService.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace galaxy::colony_management {
// Fixed owner-visible operations; the pipe accepts no raw NPC/site/quantity.
// An unavailable original facility or rejected native command cannot be
// interpreted as a construction, labor or production success.
static constexpr std::uint64_t kFirstOperation=1, kLastOperation=28;

inline std::optional<Command> propose(const Simulation& sim,PlayerId owner,
                                      EntityId station,std::uint64_t selector,
                                      std::uint64_t command_id) {
    if(!command_id || command_id>static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) ||
       sim.has_command_id(command_id) ||
       !colony_industry::local_resident(sim,owner,station) ||
       selector<kFirstOperation || selector>kLastOperation ||
       sim.tick()==std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    const auto home=colony_industry::local_source_ship(sim,station,owner);
    const auto* ship=home?sim.find(home):nullptr;
    const auto* cargo=home?sim.colony_ship(home):nullptr;
    if(!home || !ship || !cargo ||
       ship->owner_id!=owner || ship->dock_state!=DockState::Docked ||
       ship->dock_station!=station || ship->jump_state!=ShipJumpState::Ready)
        return std::nullopt;
    const auto* shop=colony_industry::native_workshop_at(sim,station,owner)?
        sim.workshop(station):nullptr;
    const auto* storage=colony_industry::native_storage_at(sim,station,owner)?
        sim.storage(station):nullptr;
    const auto* utility=sim.utility(station);
    const auto* habitat=sim.habitat(station);
    const auto* refinery=sim.station_refinery(station);
    if(utility && utility->source_ship!=home)utility=nullptr;
    if(habitat && habitat->source_ship!=home)habitat=nullptr;
    if(refinery && (refinery->owner!=owner ||
                    refinery->source_ship!=home))refinery=nullptr;

    Command c{};
    c.id=command_id;c.sequence=c.id;c.execute_tick=sim.tick()+1;
    c.issuer=owner;c.actor=home;c.target=station;c.amount=1;
    switch(selector){
      case 1: // 1 original physically module-built workshop
        if(shop || cargo->modules<1)return std::nullopt;
        c.action=Action::ConstructWorkshop;break;
      case 2: // 4-m3 original constructed warehouse, no free container
        if(storage || cargo->modules<1)return std::nullopt;
        c.action=Action::ConstructStorage;break;
      case 3:
        if(utility || cargo->modules<2)return std::nullopt;
        c.action=Action::ConstructUtility;break;
      case 4:
        if(habitat || cargo->modules<2)return std::nullopt;
        c.action=Action::ConstructHabitat;break;
      case 5:
        if(!shop || shop->finisher_installed || cargo->modules<1)
            return std::nullopt;
        c.action=Action::ConstructWorkshopFinisher;break;
      case 6:
        if(refinery || !utility || !habitat || cargo->modules<2)
            return std::nullopt;
        c.action=Action::BuildStationRefinery;break;
      case 7: case 8: case 9:
        if(!habitat || habitat->residents>=habitat->capacity)
            return std::nullopt;
        c.action=Action::TransferHabitatStock;c.amount=100;
        c.thrust_x=static_cast<int>(selector-6);break;
      case 10:
        if(!habitat || habitat->residents>=habitat->capacity)
            return std::nullopt;
        c.action=Action::AssignHabitatResidents;c.amount=1;break;
      case 11:
        if(!utility || !habitat || utility->technicians>=habitat->residents)
            return std::nullopt;
        c.action=Action::AssignUtilityTechnicians;c.amount=1;break;
      case 12:
        if(!utility || cargo->modules<1)return std::nullopt;
        c.action=Action::TransferUtilitySpares;c.amount=1;break;
      case 13:
        if(!utility || utility->delegated_maintenance)return std::nullopt;
        c.action=Action::SetUtilityMaintenance;c.amount=1;break;
      case 14: // repair only with native physical spares/conditions
        if(!utility || utility->condition>=100 || cargo->modules<1)
            return std::nullopt;
        c.action=Action::RepairUtility;c.amount=1;break;
      case 15: case 16: {
        if(!shop)return std::nullopt;
        const bool shape=selector==16;
        const auto rows=workforce02::plan_original_workshop(sim,owner,home,station);
        const auto candidate=rows[shape?1:0].suggested_npc;
        if(!candidate)return std::nullopt;
        return workforce02::approved_original_trade_training(
            sim,owner,home,station,candidate,
            shape?WorkshopTrade::Shape:WorkshopTrade::Cut,command_id,true);
      }
      case 17: case 18: {
        if(!shop)return std::nullopt;
        const auto trade=selector==18?WorkshopTrade::Shape:WorkshopTrade::Cut;
        // Strictly original named station residents; never create a person.
        // A capped scan may conservatively return no candidate.
        std::uint64_t inspected=0;
        for(const auto& [id,n]:sim.npc_records()){
            if(++inspected>4096)break;
            if(!sim.workshop_trade_certified(id,trade) ||
               n.home_ship!=home || n.location!=station ||
               n.assigned_site!=station || n.role!=NpcRole::Habitat)
                continue;
            auto proposal=workforce02::approved_workshop_supervisor(
                sim,owner,home,station,trade,id,command_id,true);
            if(proposal)return proposal;
        }
        return std::nullopt;
      }
      case 19: case 20: {
        if(!shop)return std::nullopt;
        const bool shape=selector==20;
        const auto slots=workforce02::plan_original_workshop(sim,owner,home,station);
        const auto& slot=slots[shape?1:0];
        const auto& previous=shop->shifts[shape?1:0];
        const auto worker=previous.command_id &&
            previous.end_tick<=sim.tick()+1 ?
            previous.worker:slot.suggested_npc;
        if(!worker)return std::nullopt;
        const auto start=sim.tick()+1;
        if(start>std::numeric_limits<std::uint64_t>::max()-16)
            return std::nullopt;
        return workforce02::approved_original_workshop_shift(
            sim,owner,home,station,shape,worker,start,start+16,
            command_id,true);
      }
      case 21: case 22: {
        if(!shop)return std::nullopt;
        const bool shape=selector==22;
        const auto slots=workforce02::plan_original_workshop(sim,owner,home,station);
        const auto candidate=slots[shape?1:0].suggested_npc;
        return workforce02::approved_original_assignment(
            sim,owner,home,station,shape,candidate,command_id,true);
      }
      case 23: case 24: {
        if(!shop)return std::nullopt;
        const auto trade=selector==24?WorkshopTrade::Shape:WorkshopTrade::Cut;
        const auto original=shop->approved_jobs[selector==24?1:0].command_id;
        return workforce02::approved_workshop_job_cancel(
            sim,owner,home,station,trade,original,command_id,true);
      }
      case 25: case 26:
        if(!shop ||
           !(selector==25?shop->operator_npc_id:shop->finisher_npc_id))
            return std::nullopt;
        c.action=selector==25?Action::AssignWorkshopOperator:
                               Action::AssignWorkshopFinisher;
        c.amount=0;break;
      case 27: case 28: {
        if(!shop)return std::nullopt;
        const bool shape=selector==28;
        const auto original=shop->shifts[shape?1:0].command_id;
        return workforce02::approved_original_workshop_shift_cancel(
            sim,owner,home,station,shape,original,command_id,true);
      }
      default:return std::nullopt;
    }
    return c;
}

inline std::string make_snapshot(const PersistentWorld& world,PlayerId owner,
                                 std::uint64_t session,std::uint64_t request,
                                 int result=0,std::uint64_t chosen=0){
    const auto& sim=world.world();
    const auto person=sim.player_people().find(owner);
    if(person==sim.player_people().end() ||
       !colony_industry::local_resident(sim,owner,person->second.physical_station))
        throw std::invalid_argument("industrial facility console requires colony residence");
    const auto station=person->second.physical_station;
    const auto home=colony_industry::local_source_ship(sim,station,owner);
    const auto* cargo=home?sim.colony_ship(home):nullptr;
    const auto* shop=colony_industry::native_workshop_at(sim,station,owner)?
        sim.workshop(station):nullptr;
    const auto* store=colony_industry::native_storage_at(sim,station,owner)?
        sim.storage(station):nullptr;
    const auto* habitat=sim.habitat(station);
    const auto* utility=sim.utility(station);
    const auto* refinery=sim.station_refinery(station);
    if(habitat && (!home || habitat->source_ship!=home))habitat=nullptr;
    if(utility && (!home || utility->source_ship!=home))utility=nullptr;
    if(refinery && (refinery->owner!=owner || refinery->source_ship!=home))
        refinery=nullptr;
    const auto docked=home && sim.find(home) &&
        sim.find(home)->dock_state==DockState::Docked &&
        sim.find(home)->dock_station==station;
    std::ostringstream out;
    out<<"IMHEAD|1|"<<session<<'|'<<request<<'|'<<sim.tick()<<'|'
       <<world.generation()<<'|'<<station<<'|'<<home
       <<'|'<<(docked?1:0)<<'|'<<(shop?1:0)<<'|'<<(store?1:0)
       <<'|'<<(utility?1:0)<<'|'<<(habitat?1:0)
       <<'|'<<(shop && shop->finisher_installed?1:0)
       <<'|'<<(refinery?1:0)<<'|'<<(cargo?cargo->modules:0)
       <<'|'<<(habitat?habitat->residents:0)
       <<'|'<<(utility?utility->technicians:0)
       <<'|'<<(utility?utility->stored_energy:0)
       <<'|'<<(utility && utility->powered?1:0)
       <<'|'<<(utility?utility->spare_modules:0)
       <<'|'<<(shop?shop->supervisor_npc_id:0)
       <<'|'<<(shop?static_cast<unsigned>(shop->supervisor_trade):0)
       <<'|'<<result<<'|'<<chosen<<'\n';
    if(shop && home){
        const auto rows=workforce02::plan_original_workshop(
            sim,owner,home,station);
        for(unsigned i=0;i<2;++i){
            const auto& row=rows[i];
            const auto credits=row.suggested_npc?
                sim.workshop_training_units(
                    row.suggested_npc,
                    i?WorkshopTrade::Shape:WorkshopTrade::Cut):0;
            out<<"IMWORK|1|"<<session<<'|'<<request<<'|'<<(i+1)
               <<'|'<<static_cast<unsigned>(row.gate)
               <<'|'<<static_cast<unsigned>(row.work_state)
               <<'|'<<row.assigned_npc<<'|'<<row.suggested_npc
               <<'|'<<credits<<'|'<<row.shift_end_tick<<'\n';
        }
    }
    // This is a conservative permission preview, NOT a promise of success.
    // Original submit/step performs the complete physical world validation.
    static constexpr std::uint64_t kPreviewBase=0x7000000000000000ULL;
    for(std::uint64_t selector=kFirstOperation;
        selector<=kLastOperation;++selector)
        out<<"IMOPT|1|"<<session<<'|'<<request<<'|'<<selector<<'|'
           <<(propose(sim,owner,station,selector,kPreviewBase+selector)?
              1:0)<<'\n';
    out<<"IMEND|1|"<<session<<'|'<<request<<'|'<<sim.tick()<<'|'
       <<world.generation()<<'|'<<(shop?2:0)<<'|'<<kLastOperation
       <<'|'<<result<<'|'<<chosen<<'\n';
    return out.str();
}
} // namespace galaxy::colony_management
