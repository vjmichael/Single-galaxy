#pragma once
// DISPOSITION-02: one durable evidence ledger for local territorial access.
// Neither a claim nor a trespass finding is a legal warrant or weapon license.
#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyThreatPolicy.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::colony_disposition {

enum class TerritoryFailPoint : std::uint8_t {None=0,AfterInsert=1};
enum class TerritoryEventKind : std::uint8_t {
    Claim=1, Invite=2, RevokeInvitation=3, Warning=4, Trespass=5,
    Expulsion=6
};

struct TerritoryEvent {
    std::uint64_t event_id=0,sequence=0;
    TerritoryEventKind kind=TerritoryEventKind::Claim;
    EntityId colony=0,visitor_ship=0;
    PlayerId owner=0,visitor_owner=0;
    std::uint64_t observer_npc=0,world_tick=0,world_generation=0;
    std::int64_t center_x=0,center_y=0,radius_mm=0;
    std::uint64_t deadline_tick=0,related_event=0;
    std::string previous_digest,digest;
};

struct TerritorialStanding {
    npc_response::VisitorStanding standing=npc_response::VisitorStanding::Unknown;
    bool in_claimed_region=false;
    bool invited=false;
    bool warned=false;
    bool trespass_recorded=false;
    std::uint64_t claim_event=0,warning_event=0,trespass_event=0;
    std::uint64_t grace_until=0;
};

// Opens a dedicated append-only SQLite journal. No parallel NPC, geography,
// diplomacy, transport or combat systems; source facts come from PersistentWorld.
// Authentication must be enforced by the calling trusted host, not UI input.
class TerritoryJournal {
public:
    explicit TerritoryJournal(std::string db_path);
    ~TerritoryJournal();
    TerritoryJournal(const TerritoryJournal&)=delete;
    TerritoryJournal& operator=(const TerritoryJournal&)=delete;

    bool claim(const PersistentWorld& world,PlayerId owner,EntityId colony,
               std::uint64_t event_id,std::int64_t radius_mm,
               TerritoryFailPoint fail=TerritoryFailPoint::None);
    bool invite(const PersistentWorld& world,PlayerId owner,EntityId colony,
                PlayerId visitor_owner,std::uint64_t event_id,
               TerritoryFailPoint fail=TerritoryFailPoint::None);
    bool revoke_invitation(const PersistentWorld& world,PlayerId owner,
                EntityId colony,PlayerId visitor_owner,std::uint64_t event_id);
    bool warn(const PersistentWorld& world,PlayerId owner,EntityId colony,
              EntityId foreign_ship,std::uint64_t observer_npc,
              std::uint64_t event_id,std::uint64_t grace_ticks,
              TerritoryFailPoint fail=TerritoryFailPoint::None);
    bool record_trespass(const PersistentWorld& world,PlayerId owner,
              EntityId colony,EntityId foreign_ship,std::uint64_t observer_npc,
              std::uint64_t event_id,
              TerritoryFailPoint fail=TerritoryFailPoint::None);
    bool expel(const PersistentWorld& world,PlayerId owner,EntityId colony,
              EntityId foreign_ship,std::uint64_t observer_npc,
              std::uint64_t event_id,
              TerritoryFailPoint fail=TerritoryFailPoint::None);
    TerritorialStanding standing(const PersistentWorld& world,
              EntityId colony,EntityId foreign_ship) const;
    std::vector<TerritoryEvent> history() const;
    void verify() const;
private:
    struct State;
    State* state_;
    bool append(const TerritoryEvent& event,TerritoryFailPoint fail);
};
} // namespace galaxy::colony_disposition
