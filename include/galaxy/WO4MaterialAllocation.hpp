#pragma once
// WO4-C/D: owner-directed ALLOCATION of already-existing manufactured
// material in the original ship cargo into a physically CONSTRUCTED storage.
// Only the four existing original TransferStorageStack material IDs are
// supported. No added industry recipe, fake warehouse or market balance.
// Underlying physical movement/SQLite receipts are only the ORIGINAL action.
#include "galaxy/WO4LocalAllocation.hpp"
#include "galaxy/Industry01Catalog.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace galaxy::economy04 {
enum class NativeMaterial : int {
    Aluminum=0, Panels=1, Frames=2, Scrap=3
};
inline const std::array<std::string,4>& material_codes(){
    static const std::array<std::string,4> x{{
        "good/aluminum","good/aluminum_panels",
        "good/aluminum_frames","good/aluminum_scrap"
    }};
    return x;
}
struct MaterialOffer {
    PlayerId owner=0;
    EntityId ship=0,constructed_storage_site=0;
    NativeMaterial material=NativeMaterial::Aluminum;
    Priority use=Priority::Production;
    std::int64_t requested_kg=0,proposed_kg=0;
    std::int64_t protect_original_ship_kg=0;
    std::int64_t observed_ship_kg=0,observed_storage_kg=0;
    std::uint64_t tick=0,state_hash=0;
};
inline std::optional<MaterialOffer> propose_original_storage_allocation(
    const Simulation& world,PlayerId owner,EntityId ship,EntityId site,
    NativeMaterial material,Priority use,
    std::int64_t request_kg,std::int64_t retain_in_ship_kg) {
    if(!colony03::original_owner(world,owner,ship) ||
       !colony03::original_site(world,ship,site) ||
       !world.storage(site) ||
       world.find(site)->owner_id!=owner ||
       request_kg<=0 || retain_in_ship_kg<0 ||
       (use!=Priority::Maintenance && use!=Priority::Production &&
        use!=Priority::Discretionary))
        return std::nullopt;
    const auto index=static_cast<int>(material);
    if(index<0 || index>=static_cast<int>(material_codes().size()))
        return std::nullopt;
    const auto& code=material_codes().at(static_cast<std::size_t>(index));
    // Only original configured material catalogue ids can enter storage.
    const auto& catalogue=inventory::Catalogue::standard();
    if(!catalogue.contains(code))return std::nullopt;
    const auto liters=catalogue.liters_per_unit(code);
    if(liters<=0)return std::nullopt;
    // Native storage/ship snapshots include ALL material stacks and actual
    // 4m3 constraints, including competing goods, not just this item.
    const auto shiphold=world.ship_inventory_snapshot(ship);
    const auto container=world.storage_inventory_snapshot(site);
    const auto current=shiphold.amount(code);
    const auto stored=container.amount(code);
    if(current<0 || stored<0 ||
       current<=retain_in_ship_kg)return std::nullopt;
    const auto affordable=current-retain_in_ship_kg;
    const auto by_capacity=container.free_liters()/liters;
    const auto allowed=std::min({request_kg,affordable,by_capacity});
    if(allowed<=0)return std::nullopt;
    MaterialOffer offer{};
    offer.owner=owner;offer.ship=ship;offer.constructed_storage_site=site;
    offer.material=material;offer.use=use;offer.requested_kg=request_kg;
    offer.proposed_kg=allowed;
    offer.protect_original_ship_kg=retain_in_ship_kg;
    offer.observed_ship_kg=current;offer.observed_storage_kg=stored;
    offer.tick=world.tick();offer.state_hash=world.state_hash();
    return offer;
}
inline CommitResult commit_owner_approved_storage_allocation(
    PersistentWorld& persistent,const MaterialOffer& offer,
    std::uint64_t unique_command_id,bool trusted_host_owner_approval,
    FailPoint fail=FailPoint::None) {
    CommitResult result{};result.command_id=unique_command_id;
    const auto& world=persistent.world();
    if(!trusted_host_owner_approval || !unique_command_id ||
       unique_command_id==std::numeric_limits<std::uint64_t>::max() ||
       world.has_command_id(unique_command_id) ||
       world.tick()==std::numeric_limits<std::uint64_t>::max() ||
       offer.tick!=world.tick() || offer.state_hash!=world.state_hash() ||
       offer.proposed_kg<=0 || offer.proposed_kg>100000000)
        return result;
    const auto fresh=propose_original_storage_allocation(
        world,offer.owner,offer.ship,offer.constructed_storage_site,
        offer.material,offer.use,offer.requested_kg,
        offer.protect_original_ship_kg);
    if(!fresh || fresh->proposed_kg!=offer.proposed_kg ||
       fresh->observed_ship_kg!=offer.observed_ship_kg ||
       fresh->observed_storage_kg!=offer.observed_storage_kg)
        return result;
    Command command{};
    command.id=unique_command_id;command.sequence=unique_command_id;
    command.issuer=offer.owner;command.actor=offer.ship;
    command.target=offer.constructed_storage_site;
    command.action=Action::TransferStorageStack;
    command.execute_tick=world.tick()+1;
    command.amount=offer.proposed_kg;
    command.thrust_x=static_cast<int>(offer.material);
    command.thrust_y=0; // verified existing ORIGINAL ship -> storage only
    if(!persistent.submit(command)){
        result.status=CommitStatus::OriginalCommandRejected;
        return result;
    }
    persistent.step(fail); // original transaction + native volume check
    for(auto it=persistent.world().events().rbegin();
        it!=persistent.world().events().rend();++it){
        if(it->command_id!=unique_command_id)continue;
        result.native_outcome=it->result;
        result.native_transferred_kg=it->transferred;
        result.status=it->result==Outcome::Applied &&
                      it->transferred==offer.proposed_kg?
            CommitStatus::Applied:CommitStatus::NativeDenied;
        return result;
    }
    result.status=CommitStatus::NativeDenied;
    return result;
}
} // namespace galaxy::economy04
