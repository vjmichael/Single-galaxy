#pragma once
// Stage 7 / POP-01: a deterministic, fail-closed projection of the ONE
// canonical population. No generated NPCs, no second household ledger, no
// independent immigration or supplies. World mutations stay with the
// existing authorized Simulation/PersistentWorld commands.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace galaxy::stage7 {

struct Household {
    PlanetarySiteKey site{};
    EntityId surface_location=0;
    std::uint64_t guardian_id=0, co_parent_id=0;
    std::vector<std::uint64_t> child_ids{};
    std::uint64_t booking_request=0, arrival_receipt=0;
    std::uint64_t admission_request=0, admission_tick=0;
    std::uint64_t return_request=0, return_tick=0;
    // A recorded booking, actual owner-attested admission, and return are
    // different states; neither a booking nor physical travel is residency.
    bool admitted=false, returned=false;
};

struct Census {
    std::uint64_t named_living=0, registered_dependents=0, casualties=0;
    std::uint64_t active_households=0, historical_households=0;
    std::uint64_t named_away_from_legal_home=0;
    std::map<transport::Endpoint,std::uint64_t> named_by_legal_home{};
    std::map<EntityId,std::uint64_t> dependents_by_origin_ship{};
    std::map<EntityId,std::uint64_t> physically_present{};
    // The key is (real site, original guardian). A family can return and
    // later have a distinct authentic site history without being cloned.
    std::map<std::pair<PlanetarySiteKey,std::uint64_t>,Household> households{};
    std::uint64_t all_living_people() const {
        return named_living+registered_dependents;
    }
};

inline void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}

// The overload taking source records is useful for independently testing
// corrupt/colliding identities. Production callers use project(world), which
// obtains every record from the authoritative Simulation snapshot.
inline Census project_records(
    const std::map<std::uint64_t,NpcRecord>& named,
    const std::map<std::uint64_t,PhysicalDependent>& children,
    const std::map<std::uint64_t,NpcCasualty>& dead,
    const std::map<PlanetarySiteKey,PlanetarySiteFacility>& sites) {
    Census out;
    std::set<std::uint64_t> children_in_active_homes;
    for(const auto& [id,n]:named) {
        require(id!=0 && n.id==id && n.home_ship!=0,
                "Stage 7: inconsistent original named NPC identity");
        require(dead.count(id)==0,
                "Stage 7: person simultaneously living and deceased");
        ++out.named_living;
        ++out.named_by_legal_home[n.legal_residence()];
        if(n.location!=n.legal_residence().id)
            ++out.named_away_from_legal_home;
        if(n.location)++out.physically_present[n.location];
    }
    for(const auto& [id,c]:children) {
        require(id!=0 && c.child_id==id && c.guardian_id!=0 &&
                c.home_ship!=0 && c.registration_command!=0,
                "Stage 7: unregistered or inconsistent physical dependent");
        require(named.count(id)==0 && dead.count(id)==0,
                "Stage 7: dependent conflicts with another original identity");
        require(named.count(c.guardian_id)!=0 ||
                dead.count(c.guardian_id)!=0,
                "Stage 7: dependent has no recorded original guardian");
        ++out.registered_dependents;
        ++out.dependents_by_origin_ship[c.home_ship];
        if(c.location)++out.physically_present[c.location];
    }
    for(const auto& [id,person]:dead) {
        require(id!=0 && person.identity.id==id && named.count(id)==0,
                "Stage 7: invalid original casualty tombstone");
        ++out.casualties;
    }
    for(const auto& [key,site]:sites) {
        for(const auto& [guardian,h]:site.households) {
            require(guardian!=0 && site.surface_location!=0 &&
                    site.source_ship!=0 && h.co_parent_id!=guardian,
                    "Stage 7: invalid original household identity or site");
            const bool admitted=h.admission_request!=0;
            if(admitted) {
                require(h.arrival_receipt!=0 && h.admission_tick!=0 &&
                        h.consent_mask==3 && h.co_parent_id!=0,
                        "Stage 7: incomplete two-parent admission evidence");
                require(h.return_tick==0 || h.return_request!=0,
                        "Stage 7: household return lacks original request");
            } else {
                require(h.admission_tick==0 && h.return_tick==0,
                        "Stage 7: unapproved household acquired residency");
            }
            Household view{};
            view.site=key;view.surface_location=site.surface_location;
            view.guardian_id=guardian;view.co_parent_id=h.co_parent_id;
            view.booking_request=h.booking_request;
            view.arrival_receipt=h.arrival_receipt;
            view.admission_request=h.admission_request;
            view.admission_tick=h.admission_tick;
            view.return_request=h.return_request;
            view.return_tick=h.return_tick;
            view.admitted=admitted && h.return_tick==0;
            view.returned=h.return_tick!=0;
            view.child_ids=h.child_ids;
            require(!view.child_ids.empty(),
                    "Stage 7: original household has no registered dependent");
            std::set<std::uint64_t> unique;
            for(const auto child_id:view.child_ids) {
                require(unique.insert(child_id).second,
                        "Stage 7: child repeated inside one household");
                const auto child=children.find(child_id);
                require(child!=children.end() &&
                        child->second.guardian_id==guardian,
                        "Stage 7: household child differs from physical registry");
                if(view.admitted) {
                    require(children_in_active_homes.insert(child_id).second,
                            "Stage 7: same dependent admitted to two households");
                }
            }
            if(view.admitted)++out.active_households;
            if(view.returned)++out.historical_households;
            out.households.emplace(std::make_pair(key,guardian),
                                   std::move(view));
        }
    }
    // Every living original person is counted exactly once from their own
    // registry. Household views never add people, labor, cargo, or resources.
    require(out.named_living==named.size() &&
            out.registered_dependents==children.size() &&
            out.casualties==dead.size(),
            "Stage 7: impossible original population reconciliation");
    return out;
}

inline Census project(const Simulation& world) {
    return project_records(world.npc_records(),
                           world.physical_dependents(),
                           world.npc_casualties(),
                           world.planetary_sites());
}

} // namespace galaxy::stage7
