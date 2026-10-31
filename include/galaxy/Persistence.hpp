#pragma once
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>

namespace galaxy {
// Failures occur inside an SQLite transaction, before durable COMMIT.
enum class FailPoint { None, AfterReceiptWrite, AfterSnapshotWrite };

// GRSM-01: an authoritative outcome of a committed player/host command.
// This is not a reputation, moral judgment, casualty count, or NPC belief.
struct HistoricalEvent {
    std::string event_id;             // Stable event identity: command:<command_id>
    std::uint64_t generation = 0;
    std::uint64_t tick = 0;
    std::uint64_t command_id = 0;
    EntityId actor = 0;
    EntityId target = 0;
    Action action = Action::Mine;
    Outcome result = Outcome::InvalidArgument;
    std::int64_t transferred = 0;
};

// GRSM-01b: immutable, observed physical facts, not rumors or moral judgments.
enum class PhysicalKind : int { CargoMovement=1, JourneyStarted=2, JourneyCompleted=3,
                                ShipDestroyed=4, NpcLost=5 };
struct PhysicalIncident {
    std::string incident_id;
    std::uint64_t generation=0, tick=0, command_id=0;
    PhysicalKind kind=PhysicalKind::CargoMovement;
    std::string source, destination;
    std::int64_t amount=0;
    std::string detail;
};

// GRSM-02: this is knowledge held or transmitted by individual NPCs, NOT a
// historical fact. Distorted/contrary accounts never modify physical state.
// Firsthand means *local, host-attested opportunity to witness* (not proof that
// a character's report is accurate); hearsay preserves its parent claim.
enum class ClaimFidelity : int { LocalObservation=0, Faithful=1, Embellished=2, Contrary=3 };
struct KnowledgeRecord {
    std::uint64_t id=0;
    std::string incident_id;
    std::uint64_t parent_id=0;  // 0 only for directly-attested observation
    std::uint64_t speaker_npc=0, recipient_npc=0;
    ClaimFidelity fidelity=ClaimFidelity::LocalObservation;
    int valence=0, confidence=0;  // perception, -100..100 and 0..100
    std::uint64_t generation=0, tick=0;
    std::string account;  // bounded player-independent NPC claim text
};

// GRSM-03: persistent overlapping groups and bounded, witnessed social relays.
// A group view is an interpretation grounded in a relay between two members;
// it is not a verdict or a broadcast to every member of the organization.
enum class GroupStance : int { Admiring=1, Condemning=2, Mixed=3 };
struct SocialGroup {
    std::uint64_t id=0, generation=0;
    std::string name;
    std::vector<std::uint64_t> members;
};
struct GroupView {
    std::uint64_t id=0, group_id=0, knowledge_id=0, generation=0, tick=0;
    GroupStance stance=GroupStance::Mixed;
    int confidence=0;
    std::string account;
};

// GRSM-03c: changes to membership are durable epochs. Founders remain
// immutable; a member may subsequently leave and rejoin through real contacts.
enum class GroupMembershipKind : int { Join=1, Leave=2 };
struct GroupMembershipEvent {
    std::uint64_t id=0, group_id=0, npc_id=0, sponsor_npc=0;
    std::uint64_t epoch=0, prior_id=0, generation=0, tick=0;
    GroupMembershipKind kind=GroupMembershipKind::Join;
};

// GRSM-03c.2: a host-attested *meeting opportunity*, not a rumor or
// automatic trust increase. The ordinal is a reproducible familiarity count
// for this pair, and does not assert friendship or faction membership.
struct SocialEncounter {
    std::uint64_t id=0, cycle_id=0, npc_low=0, npc_high=0;
    std::uint64_t generation=0, tick=0, slot=0, prior_id=0, ordinal=0;
};

// GRSM-03c.3: directed interpersonal rapport, distinct from evidence-trust.
// A genuine *host-attested interaction* must name one actual, same-tick
// encounter receipt. Physical co-presence alone NEVER writes one of these.
// The host is responsible for validating whether assistance/harm occurred;
// the current world engine does not yet independently simulate those acts.
enum class SocialInteractionKind : int { Assistance=1, Harm=2 };
struct SocialInteractionEvent {
    std::uint64_t id=0, encounter_id=0, observer_npc=0, actor_npc=0;
    std::uint64_t prior_id=0, generation=0, tick=0;
    SocialInteractionKind kind=SocialInteractionKind::Assistance;
    int delta=0, rapport=0; // directed score -100..100, neutral 0
};

// GRSM-03d: a particular member's *attribution* of one actual social
// interaction to a second group. Never represents unanimous group consent.
// The stance is individual and may differ from rapport toward the actor.
// Source interactions remain host-attested, NOT yet independently derived
// from committed physical assistance/harm transactions.
enum class GroupAttributionStance : int { Appreciative=1, Resentful=2, Ambivalent=3 };
struct GroupAttributionEvent {
    std::uint64_t id=0,interaction_id=0,observer_group=0,actor_group=0;
    std::uint64_t observer_npc=0,actor_npc=0,membership_seq=0,prior_id=0;
    std::uint64_t generation=0,tick=0;
    GroupAttributionStance stance=GroupAttributionStance::Ambivalent;
    int delta=0,feeling=0; // directional, contextual, [-100,+100]
};
struct GroupRelationSummary {
    std::uint64_t observer_group=0,actor_group=0;
    std::size_t current_members=0,informed_members=0;
    std::size_t favorable=0,unfavorable=0,neutral=0;
    int mean_feeling=0; // informed members only; NOT official group policy
};

// GRSM-03b: directional, evidence-linked interpersonal trust. Each event is
// an immutable record of a witnessed contact or a character's reassessment;
// it is neither historical truth nor proof of the accuracy of the allegation.
enum class SocialTieKind : int { Contact=1, Reassessment=2 };
struct SocialTieEvent {
    std::uint64_t id=0, observer_npc=0, source_npc=0, prior_id=0;
    std::uint64_t knowledge_id=0, generation=0, tick=0;
    SocialTieKind kind=SocialTieKind::Contact;
    int delta=0, trust=50;
};

class PersistentWorld {
public:
    // If the SQLite file is new, persist the supplied initial world.
    // For an existing database, load authoritative saved world and ignore initial state.
    PersistentWorld(const std::string& sqlite_path, Simulation initial_world);
    ~PersistentWorld();
    PersistentWorld(const PersistentWorld&) = delete;
    PersistentWorld& operator=(const PersistentWorld&) = delete;

    // User-invoked salvage: copy source through SQLite backup, replay verified v2
    // operations on the copy, and validate receipts. Never mutate source.
    static void recover_copy(const std::string& source, const std::string& destination);

    bool submit(const Command& command, FailPoint fail = FailPoint::None);
    // Privileged, explicit test/host-only gate administration. Not exposed as
    // a player action. Persists through the normal operation receipt and journal.
    bool submit_testing_gate_change(EntityId forward, EntityId reverse,
                                    bool online, std::uint64_t command_id,
                                    FailPoint fail = FailPoint::None);
    // Host/test-only provisioning. Not exposed to ordinary player input.
    // Staff is an absolute headcount; supplies is an added quantity.
    bool submit_testing_gate_operations(EntityId forward, EntityId reverse,
        Action action, std::int64_t amount, std::uint64_t command_id,
        FailPoint fail = FailPoint::None);
    // Host/test-only consequences; future combat/risk systems supply the
    // validated destructive cause. Never available via player command submission.
    bool submit_testing_destroy_ship(EntityId attacker, EntityId victim,
                                     std::uint64_t command_id, FailPoint fail = FailPoint::None);
    bool submit_testing_crew_loss(EntityId colony_ship, std::int64_t count,
                                  std::uint64_t command_id, FailPoint fail = FailPoint::None);
    void step(FailPoint fail = FailPoint::None);
    void steps(std::uint64_t n);
    const Simulation& world() const noexcept { return world_; }
    std::int64_t resource_origin() const noexcept { return resource_origin_; }
    std::uint64_t generation() const noexcept { return generation_; }
    // Bounded, read-only records; older v4 actions are not retroactively invented.
    std::vector<HistoricalEvent> recent_history(std::size_t limit = 50) const;
    std::vector<PhysicalIncident> recent_physical_incidents(std::size_t limit = 50) const;
    // Trusted host/test APIs; these are NOT player-submitted commands.
    // Evidence requires a new, locally observable incident on this world tick.
    // No NPC is automatically made omniscient when the historical ledger grows.
    bool record_local_observation(const std::string& incident_id, std::uint64_t npc_id);
    // The speaker is determined by the parent record's recipient. Speaker and
    // listener must currently co-occupy a location and both remain alive.
    bool transmit_claim(std::uint64_t source_record_id, std::uint64_t listener_npc,
                        ClaimFidelity fidelity, int valence, int confidence,
                        const std::string& account);
    std::vector<KnowledgeRecord> recent_knowledge(std::size_t limit = 50) const;
    // Trusted host/test-only social operations; no pre-existing organizations.
    // An initial group must be founded by 2..24 living, physically co-located
    // NPCs from one original colony; NPCs can belong to many groups.
    std::uint64_t found_social_group(const std::string& name,
                                    const std::vector<std::uint64_t>& members);
    // Same-location contact between two living MEMBERS; creates an individual
    // claim AND durable group relay evidence in one atomic transaction.
    bool relay_group_claim(std::uint64_t group_id,std::uint64_t parent_knowledge_id,
                           std::uint64_t listener,ClaimFidelity fidelity,
                           int valence,int confidence,const std::string& account);
    // View requires a verified group relay, preserving distinct perspectives.
    bool record_group_view(std::uint64_t group_id,std::uint64_t relay_knowledge_id,
                           GroupStance stance,int confidence,const std::string& account);
    std::vector<SocialGroup> recent_social_groups(std::size_t limit=50) const;
    // Host-only voluntary membership transitions: a join is attested by an
    // existing member physically meeting the applicant. A leave is self-only.
    // This is not broadcast to or learned by any other NPC automatically.
    bool change_group_membership(std::uint64_t group_id,std::uint64_t npc_id,
                                 GroupMembershipKind kind,std::uint64_t sponsor_npc=0);
    std::vector<std::uint64_t> active_social_group_members(std::uint64_t group_id) const;
    std::vector<GroupMembershipEvent> recent_group_membership(std::size_t limit=50) const;
    std::vector<GroupView> recent_group_views(std::size_t limit=50) const;
    // Directed social tie: observer's trust in source, not automatically mutual.
    // Both characters must be real survivors at the same present location.
    bool record_social_contact(std::uint64_t observer,std::uint64_t source);
    // A bounded subjective reassessment following an actual claim *received*
    // from this source. A specific claim cannot be reassessed twice.
    bool reassess_social_trust(std::uint64_t observer,std::uint64_t source,
                              std::uint64_t received_claim_id,int delta);
    // New opt-in contact-gated transmission across group boundaries. The
    // listener must know/trust the speaker. Confidence is capped by the
    // incoming claim and listener's current directed trust at COMMIT time.
    bool relay_trusted_claim(std::uint64_t parent_knowledge_id,std::uint64_t listener,
                             ClaimFidelity fidelity,int valence,int proposed_confidence,
                             const std::string& account);
    std::vector<SocialTieEvent> recent_social_ties(std::size_t limit=50) const;
    // Opt-in host-only deterministic encounter scan. Limits bound actual work
    // per call; encounters only record real co-located survivors, with a fixed
    // eight-tick same-pair cooldown. No automatic claims or trust change.
    std::size_t schedule_social_encounters(std::size_t scan_budget=16,
                                          std::size_t contact_budget=4);
    std::vector<SocialEncounter> recent_social_encounters(std::size_t limit=50) const;
    // An interaction is an EXPLICIT host decision, never an automatic result
    // of proximity or encountering. Only a real same-tick encounter can anchor
    // it; at most one directed reaction per encounter and observer/actor pair.
    bool record_social_interaction(std::uint64_t encounter_id,
              std::uint64_t observer,std::uint64_t actor,SocialInteractionKind kind);
    std::vector<SocialInteractionEvent> recent_social_interactions(std::size_t limit=50) const;
    int directed_rapport(std::uint64_t observer,std::uint64_t actor) const;
    // Explicit interpretation by an individual observer of a documented
    // reaction. Both actors must belong to the named groups *at attribution*
    // and the source reaction must be current-tick, witnessed and unreplayed.
    // A group summary merely counts distinct active members' latest views;
    // it is never a faction vote or a legal / historical truth.
    bool attribute_interaction_to_group(std::uint64_t interaction_id,
        std::uint64_t observer_group,std::uint64_t actor_group,
        GroupAttributionStance stance);
    int individual_group_feeling(std::uint64_t observer_npc,
        std::uint64_t observer_group,std::uint64_t actor_group) const;
    GroupRelationSummary group_relation_summary(std::uint64_t observer_group,
        std::uint64_t actor_group) const;
    std::vector<GroupAttributionEvent> recent_group_attributions(std::size_t limit=50) const;





private:
    class Store;
    std::unique_ptr<Store> store_;
    Simulation world_;
    std::int64_t resource_origin_ = 0;
    std::int64_t credit_origin_ = 0;
    std::uint64_t generation_ = 0;
};
} // namespace galaxy
