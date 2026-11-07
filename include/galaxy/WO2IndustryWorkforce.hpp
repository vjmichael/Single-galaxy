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
        if(sh.medic_id==npc && at_tick<sh.end_tick)
            return true;
    }
    return false;
}
inline std::set<std::uint64_t> occupied_named_workers(
    const Simulation& world,std::uint64_t at_tick) {
    std::set<std::uint64_t> occupied;
    for(const auto& entry:world.workshops()){
        const auto& shop=entry.second;
        if(shop.operator_npc_id)occupied.insert(shop.operator_npc_id);
        if(shop.finisher_npc_id)occupied.insert(shop.finisher_npc_id);
    }
    for(const auto& entry:world.medic_shifts()){
        const auto& shift=entry.second;
        if(shift.medic_id && shift.end_tick>at_tick)
            occupied.insert(shift.medic_id);
    }
    work::NativeCapacityContext context{};
    context.yards=&world.shipyard_constructions();
    context.builds=&world.freighter_build_orders();
    const auto index=work::index_native_capacity(context);
    occupied.insert(index.working_npcs.begin(),index.working_npcs.end());
    return occupied;
}
inline bool already_on_named_duty(const Simulation& world,std::uint64_t npc,
                                  std::uint64_t at_tick) {
    return !npc || occupied_named_workers(world,at_tick).count(npc)!=0;
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
    // Compute exclusions once, even for thousands of actual named NPCs.
    const auto next_tick=world.tick()+1;
    const auto occupied=occupied_named_workers(world,next_tick);
    // Prefer verifiable trade practice evidence without invalidating the
    // legacy staffing eligibility of existing installed workshops.
    // Ties go to the lowest stable original NPC identity.
    const auto preferred=[&](bool second,std::uint64_t skip)->std::uint64_t {
        const auto trade=second?WorkshopTrade::Shape:WorkshopTrade::Cut;
        std::uint64_t best=0;
        std::uint32_t best_units=0;
        for(const auto& [id,npc]:world.npc_records()){
            if(id==skip || !id || id>1000000 ||
               npc.home_ship!=home_ship || npc.location!=site ||
               npc.assigned_site!=site || npc.role!=NpcRole::Habitat ||
               npc.mentor_id || !world.npc_available_for_duty(id) ||
               occupied.count(id))continue;
            const auto units=world.workshop_training_units(id,trade);
            if(!best || units>best_units){best=id;best_units=units;}
        }
        return best;
    };
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
                row.suggested_npc=preferred(second,0);
                row.gate=row.suggested_npc?StaffGate::Ready:
                                           StaffGate::NoFreeQualifiedPerson;
            }
        }
        out.push_back(row);
    }
    // Both slots are projections from the same current world. Never
    // suggest the same free worker for two simultaneously ready positions.
    if(out.size()==2 && out[0].suggested_npc &&
       out[1].suggested_npc==out[0].suggested_npc){
        out[1].suggested_npc=preferred(true,out[0].suggested_npc);
        out[1].gate=out[1].suggested_npc?StaffGate::Ready:
                                             StaffGate::NoFreeQualifiedPerson;
    }
    return out;
}

/**
 * A bounded, read-only coverage forecast for the TWO actually installed
 * native production stations. This is NOT a durable shift reservation.
 * Future medical shifts must be checked over the whole requested interval,
 * not just at the tick when a staffing screen was opened.
 */
enum class CoverageGate : std::uint8_t {
    Provisional, InvalidWindow, SiteUnavailable, NoFreeWorker,
    FutureMedicalShift, CurrentDutyUnavailable
};
struct CoverageRow {
    bool finisher=false,incumbent=false;
    std::uint64_t start_tick=0,end_tick=0,worker=0;
    CoverageGate gate=CoverageGate::SiteUnavailable;
    // Production cannot start from this forecast; the authenticated owner
    // still has to submit the original worker command to PersistentWorld.
    bool reserved=false;
    bool requires_owner_command=true;
};
inline bool medical_shift_overlaps(const Simulation& world,std::uint64_t npc,
                                    std::uint64_t start,std::uint64_t end) {
    for(const auto& [id,shift]:world.medic_shifts()){
        (void)id;
        if(shift.medic_id==npc && start<shift.end_tick &&
           shift.start_tick<end)return true;
    }
    return false;
}
inline std::vector<CoverageRow> plan_provisional_workshop_coverage(
    const Simulation& world,PlayerId authenticated_owner,
    EntityId home_ship,EntityId site,
    std::uint64_t start_tick,std::uint64_t end_tick) {
    const auto slots=plan_original_workshop(world,authenticated_owner,home_ship,site);
    std::vector<CoverageRow> result(2);
    const bool valid_window=world.tick()<std::numeric_limits<std::uint64_t>::max() &&
        start_tick>world.tick() && end_tick>start_tick &&
        end_tick-start_tick<=10000;
    const auto occupied=occupied_named_workers(world,world.tick()+1);
    std::set<std::uint64_t> selected;
    for(std::size_t i=0;i<2;++i) {
        auto& row=result[i];
        row.finisher=i==1;row.start_tick=start_tick;row.end_tick=end_tick;
        if(!valid_window){row.gate=CoverageGate::InvalidWindow;continue;}
        const auto& slot=slots[i];
        if(slot.gate!=StaffGate::Ready &&
           slot.gate!=StaffGate::AlreadyStaffed &&
           slot.gate!=StaffGate::NoFreeQualifiedPerson) {
            row.gate=CoverageGate::SiteUnavailable;continue;
        }
        if(slot.assigned_npc) {
            // An incumbent cannot be replaced by a read-only prediction.
            row.worker=slot.assigned_npc;row.incumbent=true;
            if(!world.npc_available_for_duty(row.worker))
                row.gate=CoverageGate::CurrentDutyUnavailable;
            else if(medical_shift_overlaps(world,row.worker,start_tick,end_tick))
                row.gate=CoverageGate::FutureMedicalShift;
            else row.gate=CoverageGate::Provisional;
            selected.insert(row.worker);
            continue;
        }
        bool future_conflict=false;
        for(const auto& [id,npc]:world.npc_records()){
            if(!id || id>1000000 || npc.home_ship!=home_ship ||
               npc.location!=site || npc.assigned_site!=site ||
               npc.role!=NpcRole::Habitat || npc.transport_id ||
               npc.mentor_id || !world.npc_available_for_duty(id) ||
               selected.count(id))continue;
            // Inspect the requested interval BEFORE the current-duty index:
            // an active clinic shift is an interval conflict, not simply an
            // unexplained worker shortage.
            if(medical_shift_overlaps(world,id,start_tick,end_tick)){
                future_conflict=true;continue;
            }
            if(occupied.count(id))continue;
            row.worker=id;row.gate=CoverageGate::Provisional;
            selected.insert(id);break;
        }
        if(!row.worker)
            row.gate=future_conflict?CoverageGate::FutureMedicalShift:
                                     CoverageGate::NoFreeWorker;
    }
    return result;
}

// WO2-H: inspect receipt-backed ORIGINAL NPC credentials. These two native
// trades are the only currently executable certifications. Other profession
// profiles remain non-executable until their facilities exist.
struct TradeQualification {
    std::uint64_t npc=0;
    WorkshopTrade trade=WorkshopTrade::Cut;
    std::uint32_t proven_units=0,required_units=3;
    bool certified=false;
};
inline TradeQualification inspect_trade_qualification(
    const Simulation& world,std::uint64_t npc,WorkshopTrade trade) {
    TradeQualification row{};
    row.npc=npc;row.trade=trade;
    if(trade!=WorkshopTrade::Cut && trade!=WorkshopTrade::Shape)return row;
    row.proven_units=world.workshop_training_units(npc,trade);
    row.certified=row.proven_units>=row.required_units;
    return row;
}
// The host must authenticate 'owner' from the active session. The bool is an
// explicit owner confirmation, not a privilege to create arbitrary training.
// Real material and original workforce admission are rechecked on execution.
inline std::optional<Command> approved_original_trade_training(
    const Simulation& world,PlayerId owner,EntityId home_ship,EntityId site,
    std::uint64_t npc,WorkshopTrade trade,std::uint64_t command_id,
    bool approved_by_owner) {
    if(!approved_by_owner || !command_id || !npc || npc>1000000 ||
       command_id==std::numeric_limits<std::uint64_t>::max() ||
       world.has_command_id(command_id) ||
       world.tick()==std::numeric_limits<std::uint64_t>::max() ||
       (trade!=WorkshopTrade::Cut && trade!=WorkshopTrade::Shape))
        return std::nullopt;
    const auto rows=plan_original_workshop(world,owner,home_ship,site);
    const auto& row=rows[trade==WorkshopTrade::Shape?1:0];
    if(row.gate!=StaffGate::Ready && row.gate!=StaffGate::AlreadyStaffed &&
       row.gate!=StaffGate::NoFreeQualifiedPerson)return std::nullopt;
    const auto* shop=world.workshop(site);
    const auto* power=world.utility(site);
    const auto person=world.npc_records().find(npc);
    if(!shop || !power || !power->powered || power->condition<=0 ||
       power->stored_energy<1 ||
       power->consumed_energy==std::numeric_limits<std::int64_t>::max() ||
       shop->scrap_kg>=1000 ||
       (trade==WorkshopTrade::Cut ? shop->aluminum_kg<1 :
           !shop->finisher_installed || shop->panels_kg<1) ||
       person==world.npc_records().end() ||
       person->second.home_ship!=home_ship ||
       person->second.location!=site || person->second.assigned_site!=site ||
       person->second.role!=NpcRole::Habitat ||
       person->second.mentor_id || person->second.transport_id ||
       !world.npc_available_for_duty(npc) ||
       occupied_named_workers(world,world.tick()+1).count(npc) ||
       world.workshop_training_units(npc,trade)>=3)
        return std::nullopt;
    Command c{};
    c.id=command_id;c.sequence=command_id;
    c.execute_tick=world.tick()+1;c.issuer=owner;
    c.actor=home_ship;c.target=site;
    c.action=Action::TrainWorkshopTrade;
    c.amount=static_cast<std::int64_t>(npc);
    c.thrust_x=static_cast<int>(trade);
    return c;
}

// WO2-I: an approved finite named-worker shift uses the original future
// execute_tick as its start, and the bounded native command duration as its
// expiration. A pending order is NOT an already working person: the original
// execution boundary rechecks physical duty and may refuse it.
inline std::optional<Command> approved_original_workshop_shift(
    const Simulation& world,PlayerId owner,EntityId home_ship,EntityId site,
    bool finisher,std::uint64_t worker,std::uint64_t start_tick,
    std::uint64_t end_tick,std::uint64_t command_id,bool approved_by_owner) {
    if(!approved_by_owner || !command_id || world.has_command_id(command_id) ||
       !worker || worker>1000000 || !owner ||
       world.tick()==std::numeric_limits<std::uint64_t>::max() ||
       start_tick<=world.tick() || end_tick<=start_tick ||
       start_tick-world.tick()>10000 || end_tick-start_tick>10000 ||
       command_id==std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    const auto rows=plan_original_workshop(world,owner,home_ship,site);
    const auto& row=rows[finisher?1:0];
    const auto* shop=world.workshop(site);
    if((row.gate!=StaffGate::Ready && row.gate!=StaffGate::NoFreeQualifiedPerson) ||
       !shop || shop->shifts[finisher?1:0].command_id ||
       (finisher && !shop->finisher_installed) ||
       (finisher?shop->finisher_npc_id:shop->operator_npc_id))
        return std::nullopt;
    const auto person=world.npc_records().find(worker);
    if(person==world.npc_records().end() ||
       person->second.home_ship!=home_ship ||
       person->second.location!=site ||
       person->second.assigned_site!=site ||
       person->second.role!=NpcRole::Habitat ||
       person->second.transport_id || person->second.mentor_id ||
       !world.npc_available_for_duty(worker) ||
       already_on_named_duty(world,worker,world.tick()+1) ||
       medical_shift_overlaps(world,worker,start_tick,end_tick))
        return std::nullopt;
    for(const auto& [id,other]:world.workshops()){
        (void)id;
        for(const auto& shift:other.shifts)
            if(shift.command_id && shift.worker==worker &&
               start_tick<shift.end_tick && shift.start_tick<end_tick)
                return std::nullopt;
    }
    Command c{};
    c.id=command_id;c.sequence=command_id;c.execute_tick=start_tick;
    c.issuer=owner;c.actor=home_ship;c.target=site;
    c.action=Action::StartWorkshopShift;
    c.amount=static_cast<std::int64_t>(worker);
    c.thrust_x=finisher?2:1;
    c.thrust_y=static_cast<int>(end_tick-start_tick);
    return c;
}
// An owner can cancel an active shift OR an existing original future
// command with enough lead time for the cancellation to commit first.
// A canceled future order receives its own rejected effect on the due tick.
inline std::optional<Command> approved_original_workshop_shift_cancel(
    const Simulation& world,PlayerId owner,EntityId home_ship,EntityId site,
    bool finisher,std::uint64_t original_shift_command,
    std::uint64_t cancel_command,bool approved_by_owner) {
    if(!approved_by_owner || !cancel_command ||
       world.has_command_id(cancel_command) ||
       !original_shift_command ||
       original_shift_command>static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) ||
       world.tick()==std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    const auto rows=plan_original_workshop(world,owner,home_ship,site);
    const auto& row=rows[finisher?1:0];
    const auto* shop=world.workshop(site);
    const bool active=shop &&
        shop->shifts[finisher?1:0].command_id==original_shift_command;
    const bool pending=world.pending_workshop_shift(
        site,finisher,original_shift_command);
    if(!shop || (!active && !pending) ||
       (row.gate!=StaffGate::AlreadyStaffed &&
        row.gate!=StaffGate::Ready &&
        row.gate!=StaffGate::NoFreeQualifiedPerson))
        return std::nullopt;
    Command c{};
    c.id=cancel_command;c.sequence=cancel_command;
    c.execute_tick=world.tick()+1;c.issuer=owner;
    c.actor=home_ship;c.target=site;
    c.action=Action::CancelWorkshopShift;
    c.amount=static_cast<std::int64_t>(original_shift_command);
    c.thrust_x=finisher?2:1;
    return c;
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
