#include "galaxy/ColonyDefense.hpp"
#include <algorithm>
#include <stdexcept>
namespace galaxy::colony_defense {
std::vector<Contact> observe_local_contacts(const Simulation& world,
                                              EntityId source,std::uint64_t npc_id){
    const auto* home=world.find(source);
    const auto* workforce=world.colony_ship(source);
    const auto it=world.npc_records().find(npc_id);
    if(!home || !workforce || !npc_id ||
       home->type!=EntityType::Ship || !home->owner_id ||
       !world.passenger_needs_enabled() ||
       it==world.npc_records().end() || it->second.home_ship!=source ||
       it->second.location!=source || it->second.transport_id ||
       !world.npc_available_for_duty(npc_id))
        throw std::invalid_argument("colony watch has no physically present duty-eligible NPC");
    std::vector<Contact> contacts;
    for(const auto& [id,target]:world.entities()){
        if(id==source || target.type!=EntityType::Ship ||
           !target.owner_id || target.owner_id==home->owner_id ||
           target.system_id!=home->system_id)continue;
        const auto dx=target.position.x-home->position.x;
        const auto dy=target.position.y-home->position.y;
        // The box check precedes multiplication: huge distant coordinates
        // must never overflow squared-distance calculations.
        if(dx < -kLocalWatchRangeMm || dx>kLocalWatchRangeMm ||
           dy < -kLocalWatchRangeMm || dy>kLocalWatchRangeMm)continue;
        const auto distance=static_cast<std::uint64_t>(dx*dx+dy*dy);
        if(distance>static_cast<std::uint64_t>(
             kLocalWatchRangeMm*kLocalWatchRangeMm))continue;
        contacts.push_back({source,id,npc_id,world.tick(),distance,
                            target.owner_id,target.system_id});
    }
    return contacts; // world entity map is already stable ID ordering
}
}
