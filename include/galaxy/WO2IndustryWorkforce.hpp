#pragma once
// WO2: one read-only industrial workforce management view over the ORIGINAL
// Simulation, no cloned NPC registry, material escrow or independent work queue.
// Only AssignWorkshopOperator/Finisher are native executable here. Every other
// industry profession remains data-only until its physical facility exists.
#include "galaxy/WO2ProfessionCatalog.hpp"
#include "galaxy/WorkCapacity.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <vector>
namespace galaxy::workforce02 {

enum class StaffGate : std::uint8_t {
    Ready, InvalidOwner, MissingOriginalSite, ForeignSite,
    MissingHabitat, FinisherNotConstructed, AlreadyStaffed,
    NoFreeQualifiedPerson
};
enum class WorkState : std::uint8_t {
    ActiveWorkInProgress, ReadyToProduce, NeedsStaff, NeedsPower,
    NeedsMaintenance, NeedsInput, OutputFull, NotInstalled
};
struct Slot {
    std::string recipe_id;
    Action native_action=Action::AssignWorkshopOperator;
    StaffGate gate=StaffGate::MissingOriginalSite;
    WorkState work_state=WorkState::NotInstalled;
    EntityId site=0,home_ship=0;
    PlayerId owner=0;
    std::uint64_t assigned_npc=0,suggested_npc=0;
    // Read-only; this does NOT reserve or schedule a person.
    bool can_offer_assignment() const noexcept {
        return gate==StaffGate::Ready && suggested_npc!=0 && assigned_npc==0;
    }
};
inline bool occupied_medical_shift(const Simulation& world,std::uint64_t npc,
                                   std::uint64_t at_tick) {
    for(const auto& entry:world.medic_shifts()){
        const auto& sh=entry.second;
        if(sh.medic_id==npc && sh.start_tick<=at_tick && at_tick<sh.end_tick)
            return true;
    }
    return false;
}
inline bool already_on_named_duty(const Simulation& world,std::uint64_t npc,
                                  std::uint64_t next_tick) {
    if(!npc)return true;
    for(const auto& entry:world.workshops()) {
        const auto& shop=entry.second;
        if(shop.operator_npc_id==npc || shop.finisher_npc_id==npc)return true;
    }
    if(occupied_medical_shift(world,npc,next_tick))return true;
    // Reuse exactly the original native capacity computation; workers on
    // active shipyard and unfinished hull orders cannot also run a workshop.
    work::NativeCapacityContext context{};
    context.yards=&world.shipyard_constructions();
    context.builds=&world.freighter_build_orders();
    const auto occupied=work::index_native_capacity(context);
    return occupied.working_npcs.count(npc)!=0;
}
inline WorkState state_of(const Simulation& world,EntityId site,bool second,
                           const OutpostWorkshop& workshop) {
    if(second && !workshop.finisher_installed)return WorkState::NotInstalled;
    const auto* utility=world.utility(site);
    if(!utility || !utility->powered)return WorkState::NeedsPower;
    if(utility->condition<=0)return WorkState::NeedsMaintenance;
    if(second){
        if(workshop.reserved_panels_kg)return WorkState::ActiveWorkInProgress;
        if(workshop.panels_kg<20)return WorkState::NeedsInput;
        if(workshop.frames_kg>981 || workshop.scrap_kg>999)
            return WorkState::OutputFull;
        if(!workshop.finisher_npc_id)return WorkState::NeedsStaff;
    }else{
        if(workshop.reserved_aluminum_kg)return WorkState::ActiveWorkInProgress;
        if(workshop.aluminum_kg<20)return WorkState::NeedsInput;
        if(workshop.panels_kg>981 || workshop.scrap_kg>999)
            return WorkState::OutputFull;
        if(!workshop.operator_npc_id)return WorkState::NeedsStaff;
    }
    return WorkState::ReadyToProduce;
}
// Per-site deterministic staffing plan. The host must supply the owner
// authenticated from its session. A false owner cannot nominate labor from
// another colony, even when they guess the correct NPC/site IDs.
inline std::vector<Slot> plan_original_workshop(const Simulation& world,
    PlayerId authenticated_owner,EntityId home_ship,EntityId site) {
    const auto* ship=world.find(home_ship);
    const auto* workshop=world.workshop(site);
    const auto* habitat=world.habitat(site);
    const bool owner_ok=ship && ship->type==EntityType::Ship &&
        ship->owner_id==authenticated_owner && authenticated_owner!=0 &&
        world.colony_ship(home_ship)!=nullptr;
    const bool site_ok=workshop && workshop->source_ship==home_ship;
    std::vector<Slot> out;
    out.reserve(2);
    for(unsigned n=0;n<2;++n) {
        const bool second=n!=0;
        Slot row{};
        row.site=site;row.home_ship=home_ship;row.owner=authenticated_owner;
        row.recipe_id=second?"native_shape_aluminum_frame":"native_cut_aluminum_panel";
        row.native_action=second?Action::AssignWorkshopFinisher:Action::AssignWorkshopOperator;
        if(!owner_ok)row.gate=StaffGate::InvalidOwner;
        else if(!workshop)row.gate=StaffGate::MissingOriginalSite;
        else if(!site_ok)row.gate=StaffGate::ForeignSite;
        else if(!habitat || habitat->source_ship!=home_ship ||
                habitat->residents<=0)row.gate=StaffGate::MissingHabitat;
        else if(second && !workshop->finisher_installed)row.gate=StaffGate::FinisherNotConstructed;
        else {
            row.assigned_npc=second?workshop->finisher_npc_id:workshop->operator_npc_id;
            row.work_state=state_of(world,site,second,*workshop);
            if(row.assigned_npc)row.gate=StaffGate::AlreadyStaffed;
            else {
                row.gate=StaffGate::NoFreeQualifiedPerson;
                const auto next_tick=world.tick()+1;
                for(const auto& [id,npc]:world.npc_records()){
                    if(id==0 || id>1000000 || npc.home_ship!=home_ship ||
                       npc.location!=site || npc.assigned_site!=site ||
                       npc.role!=NpcRole::Habitat ||
                       npc.mentor_id || !world.npc_available_for_duty(id) ||
                       already_on_named_duty(world,id,next_tick))
                        continue;
                    // Native workshop baseline does not infer a new skill
                    // license. A constructed station and existing Habitat
                    // worker role are the real credentials for now.
                    row.suggested_npc=id;row.gate=StaffGate::Ready;break;
                }
            }
        }
        out.push_back(row);
    }
    // Both slots are projections from the same current world. Never
    // suggest the same free worker for two simultaneously ready positions.
    if(out.size()==2 && out[0].suggested_npc &&
       out[1].suggested_npc==out[0].suggested_npc){
        out[1].gate=StaffGate::NoFreeQualifiedPerson;
        out[1].suggested_npc=0;
        const auto next_tick=world.tick()+1;
        for(const auto& [id,npc]:world.npc_records()){
            if(id==out[0].suggested_npc || id==0 || id>1000000 ||
               npc.home_ship!=home_ship || npc.location!=site ||
               npc.assigned_site!=site || npc.role!=NpcRole::Habitat ||
               npc.mentor_id || !world.npc_available_for_duty(id) ||
               already_on_named_duty(world,id,next_tick))continue;
            out[1].suggested_npc=id;
            out[1].gate=StaffGate::Ready;
            break;
        }
    }
    return out;
}
// Builds ONE original Command only following explicit owner/host approval.
// Does not submit, reserve, allocate a command ID or claim success. Caller
// uses the original PersistentWorld::submit and later verifies its Event.
inline std::optional<Command> approved_original_assignment(
    const Simulation& world,PlayerId authenticated_owner,
    EntityId home_ship,EntityId site,bool finisher,
    std::uint64_t worker,std::uint64_t command_id,bool approved_by_owner) {
    if(!approved_by_owner || !command_id || !worker ||
       command_id==std::numeric_limits<std::uint64_t>::max() ||
       world.tick()==std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    const auto rows=plan_original_workshop(world,authenticated_owner,home_ship,site);
    const auto& row=rows.at(finisher?1:0);
    if(!row.can_offer_assignment() || row.suggested_npc!=worker)
        return std::nullopt;
    Command command{};
    command.id=command_id;
    command.sequence=command_id;
    command.execute_tick=world.tick()+1;
    command.issuer=authenticated_owner;
    command.actor=home_ship;
    command.target=site;
    command.action=row.native_action;
    command.amount=static_cast<std::int64_t>(worker);
    return command;
}
} // namespace galaxy::workforce02
