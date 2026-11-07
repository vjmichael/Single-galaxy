#pragma once
// WO4-C: the ONLY resource-moving route in this module is the existing,
// fully authenticated original ship -> built outpost facility action.
// Trusted host must establish caller identity before supplying owner_approved.
// A WO4 snapshot or grant is never an approval or an inventory reservation.
#include "galaxy/WO4Allocation.hpp"
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace galaxy::economy04 {
enum class CommitStatus : std::uint8_t {
    Refused=0,OriginalCommandRejected=1,NativeDenied=2,Applied=3
};
struct CommitResult {
    CommitStatus status=CommitStatus::Refused;
    std::uint64_t command_id=0;
    Outcome native_outcome=Outcome::InvalidArgument;
    std::int64_t native_transferred_kg=0;
};
// Idempotence/restart are inherited from original submitted Command ids,
// original journal and original SQLite snapshots. Each *accepted* submission
// executes at the next original world tick and can be recovered normally.
// The queued command and its execution are NOT a single new WO4 transaction.
inline CommitResult commit_owner_approved_local_vital(
    PersistentWorld& persistent,const Grant& offer,
    AllocationPolicy policy,std::uint64_t unique_command_id,
    bool trusted_host_authenticated_owner_and_explicit_approval) {
    CommitResult r{};r.command_id=unique_command_id;
    const auto& world=persistent.world();
    if(!trusted_host_authenticated_owner_and_explicit_approval ||
       !unique_command_id ||
       unique_command_id==std::numeric_limits<std::uint64_t>::max() ||
       world.has_command_id(unique_command_id) ||
       world.tick()==std::numeric_limits<std::uint64_t>::max() ||
       offer.scope!=TransferScope::OriginalLocalCommand ||
       offer.source_ship!=offer.source_location ||
       offer.owner==0 || offer.owner==Simulation::kServerAuthority ||
       offer.quantity_kg<=0 || offer.quantity_kg>100000000 ||
       (offer.destination_kind!=CompartmentKind::OutpostHabitat &&
        offer.destination_kind!=CompartmentKind::OutpostFarm) ||
       offer.destination_location==offer.source_ship ||
       offer.observed_tick!=world.tick() ||
       offer.observed_state_hash!=world.state_hash())
        return r;
    const auto live=observe_owner_world(world,offer.owner,offer.source_ship,
                                        policy.reserves);
    if(!live)return r;
    const auto plan=plan_local_vitals(*live,world,policy);
    if(!plan)return r;
    const auto matches=[&](const Grant& g){
        return g.owner==offer.owner &&
               g.source_ship==offer.source_ship &&
               g.source_location==offer.source_location &&
               g.destination_location==offer.destination_location &&
               g.destination_kind==offer.destination_kind &&
               g.vital==offer.vital && g.priority==offer.priority &&
               g.scope==offer.scope && g.quantity_kg==offer.quantity_kg &&
               g.observed_tick==offer.observed_tick &&
               g.observed_state_hash==offer.observed_state_hash;
    };
    if(std::none_of(plan->grants.begin(),plan->grants.end(),matches))
        return r; // forged, altered, expired or otherwise invalid offer
    // Do not invent site proximity, facility ownership or free logistics.
    // Original Simulation::execute independently re-checks range, custody,
    // installed facility and resources before it can transfer one kg.
    Command command{};
    command.id=unique_command_id;command.sequence=unique_command_id;
    command.issuer=offer.owner;command.actor=offer.source_ship;
    command.target=offer.destination_location;
    command.execute_tick=world.tick()+1;
    command.action=offer.destination_kind==CompartmentKind::OutpostHabitat?
        Action::TransferHabitatStock:Action::TransferFarmStock;
    command.amount=offer.quantity_kg;
    command.thrust_x=static_cast<int>(offer.vital)+1;
    if(!persistent.submit(command)){
        r.status=CommitStatus::OriginalCommandRejected;
        return r;
    }
    // One durable original simulation step. It also advances other
    // legitimate native jobs; WO4 does not claim isolated clock ownership.
    persistent.step();
    const auto& events=persistent.world().events();
    for(auto it=events.rbegin();it!=events.rend();++it){
        if(it->command_id!=unique_command_id)continue;
        r.native_outcome=it->result;
        r.native_transferred_kg=it->transferred;
        r.status=it->result==Outcome::Applied &&
            it->transferred==offer.quantity_kg?
            CommitStatus::Applied:CommitStatus::NativeDenied;
        return r;
    }
    r.status=CommitStatus::NativeDenied;
    return r;
}
} // namespace galaxy::economy04
