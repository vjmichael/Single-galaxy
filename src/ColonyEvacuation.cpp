#include "galaxy/ColonyEvacuation.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace galaxy::colony_evacuation {
Manifest survey(const Simulation& world,EntityId home_ship){
    const auto* home=world.colony_ship(home_ship);
    const auto* entity=world.find(home_ship);
    if(!home||!entity||entity->type!=EntityType::Ship||home->crew<0)
        throw std::invalid_argument("whole-colony survey requires intact source colony ship");
    Manifest m;
    m.home_ship=home_ship;m.world_tick=world.tick();
    m.declared_population=static_cast<std::uint64_t>(home->crew);
    for(const auto& [site,f]:world.farms()){
        (void)site;
        if(f.source_ship==home_ship){
            if(f.workers<0)throw std::logic_error("negative colony farm workers");
            m.declared_population+=static_cast<std::uint64_t>(f.workers);
        }
    }
    for(const auto& [site,h]:world.habitats()){
        (void)site;
        if(h.source_ship==home_ship){
            if(h.residents<0)throw std::logic_error("negative colony habitat residents");
            m.declared_population+=static_cast<std::uint64_t>(h.residents);
        }
    }
    // Identity set comes ONLY from authoritative living named NPCs.
    // Casualty tombstones are not living passengers.
    bool locations_valid=true;
    for(const auto& [id,n]:world.npc_records()){
        if(n.home_ship!=home_ship)continue;
        m.named_people.push_back(id);
        if(!n.location||!world.find(n.location)||
           (n.transport_id && world.voyage_status(n.transport_id)!=1 &&
                              world.voyage_status(n.transport_id)!=3))
            locations_valid=false;
        ++m.people_by_physical_location[n.location];
    }
    m.roster_complete=world.passenger_needs_enabled() &&
        m.declared_population==m.named_people.size();
    m.physically_reconciled=m.roster_complete && locations_valid;
    // Nearby vessels are candidates, NEVER pre-authorized recipients.
    // Route checks, ship control, actual seat reservation and guardian
    // consent belong to a later canonical writer transaction.
    for(const auto& [id,c]:world.colony_ships()){
        if(id==home_ship || c.crew<=0)continue;
        const auto* e=world.find(id);
        const auto* stock=world.life_support(id);
        if(!e||e->type!=EntityType::Ship || !stock)continue;
        ReceiverCandidate x;
        x.ship=id;
        const auto occupied_berths=world.passengers_aboard(id);
        x.vacant_passenger_berths=occupied_berths<16?16-occupied_berths:0;
        const auto dx=e->position.x-entity->position.x;
        const auto dy=e->position.y-entity->position.y;
        const auto range=Simulation::kDockingRangeMm;
        x.locally_accessible=e->system_id==entity->system_id &&
            dx>=-range && dx<=range && dy>=-range && dy<=range &&
            dx*dx+dy*dy<=range*range;
        const auto present=world.passenger_needs_enabled()?
            world.people_physically_aboard(id):
            static_cast<std::uint64_t>(c.crew)+world.passengers_aboard(id);
        const auto minimum=std::min({stock->food_kg,stock->water_kg,stock->oxygen_kg});
        if(minimum>0 && static_cast<std::uint64_t>(minimum/2)>present)
            x.funded_two_period_berths=std::min<std::uint64_t>(
                x.vacant_passenger_berths,
                static_cast<std::uint64_t>(minimum/2)-present);
        m.receiving_vessels.push_back(x);
    }
    std::uint64_t total_funded=0;
    for(const auto& candidate:m.receiving_vessels)
        if(candidate.locally_accessible)
            total_funded+=candidate.funded_two_period_berths;
    m.sufficient_local_berths=m.roster_complete &&
                              total_funded>=m.declared_population;
    // Explicit refuse-to-claim-success boundary. The production world has no
    // atomically verified last-crew/mothball or family/dependent departure adapter.
    m.dependent_guardianship_verified=false;
    m.last_operating_crew_departure_supported=false;
    m.executable=false;
    return m;
}
} // namespace galaxy::colony_evacuation
