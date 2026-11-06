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
    // No disclosed culprit until a native, authenticated hail links the
    // exact original physical hull. A description is NOT a unique hull ID.
    std::uint8_t sensor_detail=0,record_version=2;
    std::string observed_description;
    bool confusion_applied=false;
    std::string previous_digest,digest; // integrity checksum, not a signature
};
struct CyberIncidentSummary {
    CyberIncidentCase evidence{};
    bool witnessed_attempt=false;
    bool disrupted_modules=false;
    bool independent_law_review_required=false;
    // Only a subsequently proven authenticated hail can supply a controller
    // identity. Not evidence this person was piloting at incident time.
    PlayerId later_identified_controller=0;
    std::uint64_t identity_hail_event=0;
    bool possible_signature_match_only=false;
    // The security desk can notify, isolate and file an incident; only a
    // later genuine faction court/authority ledger may grant legal sanctions.
    bool legal_crime_finding=false;
    bool arrest_authorized=false,damage_authorized=false;
    bool seizure_authorized=false;
};

struct CyberHailIdentityLink {
    std::uint64_t sequence=0,case_id=0,hail_event=0;
    std::uint64_t world_tick=0,world_generation=0;
    PlayerId authenticated_controller=0;
    EntityId verified_ship=0;
    std::string previous_digest,digest;
};
struct CyberSignatureAlert {
    std::uint64_t case_id=0;
    std::string observed_description;
    std::uint8_t sensor_detail=0;
    std::uint64_t contact_tick=0;
    // A matching description is a lead only; never a verified actor.
    bool possible_match=true;
    bool legal_arrest=false,legal_attack=false;
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
    std::vector<CyberHailIdentityLink> hail_history() const;
    // Calls must bind authenticated_owner from the trusted session AND
    // prove a genuine original territorial hail response. A fake string
    // comparison or current native owner without a hail cannot identify.
    bool link_authenticated_hail(
        const PersistentWorld& world,const TerritoryJournal& territory,
        EntityId jurisdiction_colony,std::uint64_t observer_npc,
        EntityId visitor_ship,PlayerId authenticated_owner,
        std::uint64_t actual_hail_reply_event);
    // Original fitted sensors + duty-eligible NPC watch. Return anonymous
    // physical matches at the observed detail level, not player identities.
    std::vector<CyberSignatureAlert> watch_known_descriptions(
        const PersistentWorld& world,PlayerId colony_owner,EntityId colony,
        std::uint64_t observer_npc) const;
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
