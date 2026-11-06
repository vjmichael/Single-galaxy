#pragma once
// SHIP-STATUS-07: independent, immutable intake of contemporaneously witnessed
// native cyber incidents. This is an evidence index, not a second world writer,
// tribunal, crime finding, arrest/damage/seizure authority, or combat executor.
#include "galaxy/TerritorialCyberEvidence.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::colony_disposition {

enum class CyberIntakeFailPoint : std::uint8_t {
    None=0,AfterInsert=1
};
// A case may document an ATTEMPT that the native ship resisted; neither
// an attempt nor a disruption is, by itself, a legal crime conviction.
struct CyberIncidentCase {
    std::uint64_t sequence=0,case_id=0,command_id=0;
    std::uint64_t world_tick=0,world_generation=0,observer_npc=0;
    EntityId jurisdiction_colony=0,attacking_ship=0,victim_ship=0;
    PlayerId attacker_owner=0,victim_owner=0;
    bool confusion_applied=false;
    std::string previous_digest,digest; // integrity checksum, not a signature
};
struct CyberIncidentSummary {
    CyberIncidentCase evidence{};
    bool witnessed_attempt=false;
    bool disrupted_modules=false;
    bool independent_law_review_required=false;
    // The security desk can notify, isolate and file an incident; only a
    // later genuine faction court/authority ledger may grant legal sanctions.
    bool legal_crime_finding=false;
    bool arrest_authorized=false,damage_authorized=false;
    bool seizure_authorized=false;
};

class CyberIncidentJournal {
public:
    explicit CyberIncidentJournal(std::string original_world_db_path);
    ~CyberIncidentJournal();
    CyberIncidentJournal(const CyberIncidentJournal&)=delete;
    CyberIncidentJournal& operator=(const CyberIncidentJournal&)=delete;

    // Trusted host must bind the observer to a physically present NPC.
    // Original SHIP-STATUS-04 same-tick native outcome, real original
    // territorial claim and current named witness are all rechecked here.
    // No arbitrary client case data, attacker identity, verdict or force.
    bool intake(const PersistentWorld& world,const TerritoryJournal& territory,
        EntityId colony,std::uint64_t observer_npc,
        std::uint64_t original_attack_command,std::uint64_t case_id,
        CyberIntakeFailPoint fail=CyberIntakeFailPoint::None);
    std::vector<CyberIncidentCase> history() const;
    std::vector<CyberIncidentSummary> summaries() const;
    // Host must supply authenticated_owner from its session, not a client
    // field. A foreign caller cannot enumerate a colony's security cases.
    std::vector<CyberIncidentSummary> for_colony_owner(
        const PersistentWorld& world,PlayerId authenticated_owner,
        EntityId colony) const;
    void verify() const;
private:
    struct State;
    State* state_;
};
} // namespace galaxy::colony_disposition
