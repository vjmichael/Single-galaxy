#pragma once
#include "galaxy/Simulation.hpp"
#include "galaxy/ColonySafety.hpp"
#include "galaxy/ColonySafetyNotice.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <functional>
#include <sqlite3.h>

namespace galaxy {
namespace industry {class RegionalProductionNetwork;}
namespace shipping {class ThirdPartyRegistry;}
// Failures occur inside an SQLite transaction, before durable COMMIT.
enum class FailPoint { None, AfterReceiptWrite, AfterSnapshotWrite, AfterFirstSplitReceiver };

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

// GRSM-04a: one individual interpretation of an ACTUAL physical incident,
// anchored to an authenticated firsthand observation. Contextual opinions are
// subjective, not physical facts, proof of an NPC culprit, or an institution's
// official policy. 0 context means the person's unaffiliated viewpoint.
enum class PhysicalAppraisalStance : int { Beneficial=1, Damaging=2, Uncertain=3 };
struct PhysicalAppraisalEvent {
    std::uint64_t id=0, observation_id=0, observer_npc=0, context_group=0;
    std::uint64_t membership_seq=0, generation=0, tick=0;
    std::string incident_id;
    PhysicalAppraisalStance stance=PhysicalAppraisalStance::Uncertain;
    int confidence=0; // subjective certainty 0..100, NOT testimony proof
};

// GRSM-04b.A: narrow, objectively established roles on a committed physical
// incident. This is NOT a perpetrator/beneficiary verdict. An automated ship
// action never names a guilty NPC. New roles require their own source evidence.
enum class VerifiedNpcRole : int { Victim=1, FirsthandObserver=2 };
struct IncidentRoleReceipt {
    std::uint64_t id=0, npc_id=0, observation_id=0, generation=0, tick=0;
    std::string incident_id;
    VerifiedNpcRole role=VerifiedNpcRole::Victim;
};

// GRSM-04b.B1: a named onboard crew member may be assigned as the executor
// of an actual authoritative mining command. Assignment is not proof that a
// command succeeded; execution is independently reconciled to real ore moved.
// This is a deliberately narrow, host-controlled work path, not a general
// inference of criminal responsibility for ship / facility commands.
struct NpcMiningAssignment {
    std::uint64_t id=0, command_id=0, npc_id=0, ship_id=0, rock_id=0;
    std::uint64_t generation=0, tick=0;
};
struct NpcMiningExecution {
    std::uint64_t id=0, assignment_id=0, command_id=0, npc_id=0;
    std::uint64_t generation=0, tick=0;
    std::int64_t amount=0;
    std::string incident_id;
};

// GRSM-04b.B2a: explicitly assigned basic fabrication, distinct from
// simply being aboard the ship, and bound to a real committed material effect.
// 'issuer' is the owner who authorized the job, never an inferred worker.
struct NpcFabricationAssignment {
    std::uint64_t id=0, command_id=0, npc_id=0, ship_id=0, issuer=0;
    Action action=Action::FabricateModule;
    EntityId material_code=0;
    std::int64_t quantity=0;
    std::uint64_t generation=0, tick=0;
};
struct NpcFabricationExecution {
    std::uint64_t id=0, assignment_id=0, command_id=0, npc_id=0;
    std::uint64_t generation=0, tick=0;
    std::int64_t transferred=0;
    std::string incident_id;
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

// DEMOG-05q6b: explicit host-only binding for all newly deceased named NPCs
// in ONE scheduled terminal ferry incident. No implicit population updates.
struct SocietyDeathBinding {
    std::uint64_t event_id=0, npc_id=0;
    EntityId home_ship=0;
    std::uint64_t expected_family_revision=0;
    std::string regional_colony;
};
struct SocietyAtomicPlan {
    std::uint64_t expected_world_generation=0, command_id=0;
    std::uint64_t expected_region_revision=0, expected_owner_generation=0;
    std::uint64_t family_seed=0;
    std::string family_scope, region_scope;
    std::vector<SocietyDeathBinding> deaths;
};

// DEMOG-05q6c: cohort-only birth is a reserved ART02 dependent identity,
// not a playable NPC or a free source of labor. Trusted host admission only.
struct SocietyBirthPlan {
    std::uint64_t expected_world_generation=0;
    std::uint64_t expected_region_revision=0, expected_owner_generation=0;
    std::uint64_t family_seed=0, expected_family_revision=0;
    std::uint64_t event_id=0, child_id=0, parent_a=0, parent_b=0;
    EntityId home_ship=0;
    std::string family_scope, region_scope, regional_colony;
};

// DEMOG-05q6d: host-authorized administrative migration AFTER a committed
// voyage arrival and disembarkation. Only one named adult per transaction;
// dependent household transfer requires separate policies and manifests.
struct SocietyMigrationPlan {
    std::uint64_t expected_world_generation=0,command_id=0;
    std::uint64_t expected_region_revision=0,expected_owner_generation=0;
    std::uint64_t family_seed=0,expected_origin_family_revision=0;
    std::uint64_t expected_destination_family_revision=0;
    std::uint64_t npc_id=0,transport_id=0;
    // 05q6e4: optionally migrate the second physically arrived parent in the SAME step.
    std::uint64_t co_parent_id=0,co_parent_event_id=0;
    // 05q6e: registered dependent cohort IDs, each with a distinct immutable
    // family-move receipt ID. No simulated passenger entity is created.
    struct DependentMove { std::uint64_t child_id=0, event_id=0; };
    std::vector<DependentMove> dependents;
    EntityId origin=0,destination=0;
    std::string family_scope,region_scope,origin_colony,destination_colony;
};

// DEMOG-05q6e2: ART02 dependent identity has physical custody in the
// trusted world SQLite transaction, without creating a playable NPC.
struct DependentTravelPlan {
    std::uint64_t expected_world_generation=0, command_id=0;
    std::uint64_t transport_id=0,guardian_id=0, family_seed=0;
    // q6e3a: optional physically present second parent; both must be on
    // the same real ferry command and be recorded in the berth receipt.
    std::uint64_t co_guardian_id=0,expected_owner_generation=0;
    EntityId ferry=0,origin=0,destination=0;
    std::string family_scope,region_scope;
    std::vector<std::uint64_t> child_ids; // canonical sorted, complete guardian manifest
    bool boarding=true;                 // false: disembarkation
};
// q6e3b: explicit nonfatal emergency evacuation of ALL dependent passengers.
// A rescue is a new real voyage and cannot silently delete a prior ferry leg.
struct DependentRescuePlan {
    std::uint64_t expected_world_generation=0,command_id=0;
    std::uint64_t original_transport=0,rescue_transport=0;
    std::uint64_t guardian_id=0,co_guardian_id=0, family_seed=0;
    EntityId original_ferry=0,rescue_ferry=0,origin=0,destination=0;
    std::string family_scope,region_scope;
    std::uint64_t expected_owner_generation=0;
    std::vector<std::uint64_t> child_ids;
};
struct DependentTravelRecord {
    std::uint64_t child_id=0,transport_id=0,guardian_id=0,co_guardian_id=0;
    std::uint64_t transfer_from_transport=0;
    unsigned termination_kind=0; // 0=normal destination, 1=evacuated to next leg
    EntityId ferry=0,origin=0,destination=0;
    std::uint64_t board_command=0,arrival_command=0;
    std::uint64_t board_generation=0,arrival_generation=0;
    unsigned status=0; // 1=aboard actual ferry, 2=arrived physical destination
};

// DEMOG-05q6e5a: read-only incident custody audit. These are VERIFIED
// current passenger states, NOT outcomes of injury, disappearance, or death.
// Unaccounted and deceased minors have no authorized transition in v44/v45a;
// missing evidence is an error rather than an invented outcome.
enum class DependentVerifiedCustody : unsigned {
    Aboard=1, Stranded=2, RescuedAboard=3, Arrived=4, RescuedStranded=5
};
struct DependentIncidentMember {
    std::uint64_t child_id=0, guardian_id=0, co_guardian_id=0;
    std::uint64_t initial_transport=0, current_transport=0;
    EntityId current_ferry=0;
    DependentVerifiedCustody custody=DependentVerifiedCustody::Aboard;
    bool ever_rescued=false;
};
struct DependentIncidentAudit {
    std::uint64_t world_generation=0, initial_transport=0;
    std::size_t aboard=0, stranded=0, rescued_aboard=0, arrived=0;
    std::size_t rescued_stranded=0;
    std::size_t ever_rescued=0;
    // There is deliberately no death/unaccounted count in this schema;
    // those states require their own independently verified incident ledger.
    std::vector<DependentIncidentMember> members;
};

// DEMOG-05q6e5b1: a host-attested, historical NONFATAL stranding case.
// The roster exactly exhausts a real ferry's current passenger custody.
// A stranded or rescued-and-stranded dependent is accounted for, NOT missing.
// No death, injury or family population effect can be authorized here.
struct DependentStrandingCasePlan {
    std::string case_id, family_scope;
    std::uint64_t expected_world_generation=0;
    std::uint64_t original_transport=0, current_transport=0;
    std::uint64_t stranding_command_id=0;
    EntityId ferry=0;
    std::vector<std::uint64_t> adult_ids, child_ids; // sorted, complete, unique
};

// DEMOG-05q6e5b2: host-attested NONFATAL outcome of a *recorded* stranding.
// This is a recovery proof of the actual rescue and parent custody, not a
// new physical/legal transfer or a missing/death determination.
struct DependentRescueOutcomePlan {
    std::string outcome_id,case_id,family_scope;
    std::uint64_t expected_world_generation=0,rescue_command_id=0;
    std::uint64_t source_transport=0,rescue_transport=0;
    EntityId receiving_ferry=0;
    std::uint64_t receiving_guardian_id=0; // must be actual parent aboard receiving ferry
    std::vector<std::uint64_t> child_ids; // exact original stranded roster
};
struct DependentRescueOutcome {
    std::string outcome_id,case_id;
    std::uint64_t rescue_generation=0,receiving_guardian_id=0;
    std::uint64_t receiving_transport=0;
    EntityId receiving_ferry=0;
    std::vector<std::uint64_t> child_ids;
    // Current custody comes from verified world/child ledgers, never from
    // this immutable historical receipt alone.
    DependentIncidentAudit current_custody;
};

// DEMOG-05q6e5b3a: explicit host-adjudicated, immutable approval of a
// NON-PARENT legal guardian. These are NOT inferred from physical rescue.
// Each real parent authorizes the full dependent manifest separately. Consent
// attribution is a trusted-host receipt, NOT an NPC cryptographic signature.
struct NonParentGuardianApprovalPlan {
    std::string authorization_id,rescue_outcome_id,family_scope;
    std::uint64_t expected_world_generation=0;
    std::uint64_t approving_parent_id=0,receiving_guardian_id=0;
    std::vector<std::uint64_t> child_ids;
};
struct NonParentGuardianshipPlan {
    std::string transfer_id,rescue_outcome_id,family_scope;
    std::uint64_t expected_world_generation=0,receiving_guardian_id=0;
    std::vector<std::string> authorizations; // exactly one per recorded parent
    std::vector<std::uint64_t> child_ids; // complete rescued-family manifest
};
struct VerifiedNonParentGuardianship {
    std::string transfer_id,rescue_outcome_id;
    std::uint64_t guardian_id=0,world_generation=0;
    std::vector<std::uint64_t> child_ids;
};

// 05q6e5b4a: a real, distinct physical escort/custody handoff on the
// receiving vessel. Original birth parentage, rescue guardians and voyage
// history remain immutable; the latest independently certified escort is an
// additional authoritative custody assignment, not a worker NPC clone.
struct DependentPhysicalHandoffPlan {
    std::string handoff_id, legal_transfer_id, family_scope;
    std::uint64_t expected_world_generation=0, receiving_guardian_id=0;
    std::uint64_t transport_id=0;
    EntityId ferry=0;
    std::vector<std::uint64_t> child_ids;
};
struct VerifiedDependentPhysicalHandoff {
    std::string handoff_id, legal_transfer_id;
    std::uint64_t receiving_guardian_id=0, generation=0, transport_id=0;
    EntityId ferry=0;
    std::vector<std::uint64_t> child_ids;
};

// Two or more distinct family registries, one REAL world ferry command.
// Every child on a source ferry is required; no partial or synthetic rescue.
struct DependentFamilyManifest {
    std::string family_scope,region_scope;
    std::uint64_t family_seed=0,guardian_id=0,co_guardian_id=0;
    std::uint64_t expected_owner_generation=0;
    std::vector<std::uint64_t> child_ids;
};
struct MultiDependentTransportPlan {
    std::uint64_t expected_world_generation=0,command_id=0,transport_id=0;
    EntityId ferry=0,origin=0,destination=0;
    bool boarding=true;
    std::vector<DependentFamilyManifest> families; // sorted by family_scope
};
struct MultiDependentRescuePlan {
    std::uint64_t expected_world_generation=0,command_id=0;
    std::uint64_t original_transport=0,rescue_transport=0;
    EntityId original_ferry=0,rescue_ferry=0,origin=0,destination=0;
    std::vector<DependentFamilyManifest> families; // complete source-ferry roster
};

// DEMOG-05q6e5b5a: strictly admitted two-vessel ALLOCATION INTENT.
// This does not perform the split: existing ferry evacuation remains all-or-
// nothing to one receiver until a separate atomic two-receiver world command
// is implemented and verified. The reservation is immutable but non-binding.
struct SplitRescueReceiver {
    EntityId ferry=0;
    std::uint64_t new_transport_id=0;
    std::vector<DependentFamilyManifest> families;
};
struct SplitRescueIntentPlan {
    std::string intent_id;
    std::uint64_t expected_world_generation=0,source_transport=0;
    EntityId source_ferry=0,origin=0,destination=0;
    std::vector<SplitRescueReceiver> receivers; // exactly 2, sorted by ferry ID
};
struct VerifiedSplitRescueIntent {
    std::string intent_id;
    std::uint64_t generation=0,source_transport=0;
    std::size_t families=0,adults=0,children=0;
    std::vector<EntityId> receiving_ferries;
};

class PersistentWorld {
    friend class industry::RegionalProductionNetwork;
    friend class shipping::ThirdPartyRegistry;
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

    // v66: Resolve life-safety admission using the authoritative loaded world and
    // journal the result in the SAME SQLite connection as all other world facts.
    // No production control is applied by this preflight-only entry point.
    colony_safety::Decision record_colony_safety_preflight(EntityId home_ship,
        EntityId site,PlayerId issuer,colony_safety::Order order);
    std::uint64_t colony_safety_evidence_count() const;
    // AUTO-04D.2: committed threshold transitions, not actual rescues.
    std::uint64_t colony_response_event_count() const;
    // Read-only host queue. Presence at event tick is only an observation opportunity.
    std::vector<colony_safety::notice::Pending> pending_colony_notifications(std::uint64_t limit=100) const;
    // Trusted-host-only delivered-message receipt, not a GRSM belief or social judgment.
    bool acknowledge_colony_notification(std::uint64_t event_id,std::uint64_t npc_id);
    std::uint64_t colony_notification_opportunity_count() const;
    std::uint64_t colony_notification_acknowledgement_count() const;
    bool submit(const Command& command, FailPoint fail = FailPoint::None);
    // Host-only: admits a receipt-reserved third-party shipment under the writer lock.
    bool submit_host_consigned_transfer(const Command&, FailPoint=FailPoint::None);
    bool submit_host_passenger_action(const Command& command, FailPoint fail = FailPoint::None);
    // AUTO-04D.3E: narrow passenger-only, physical-ferry rescue gate.
    // This is NOT a colony household evacuation or an executed rescue receipt.
    // Submitted host voyage still requires a committed successful step().
    bool submit_host_emergency_ferry_rescue(const Command& command,
                                            FailPoint fail=FailPoint::None);
    // v58: full, bounded ADULT colony in one local physically stocked receiver.
    // Admission schedules only; execution revalidates and atomically commits
    // ALL living named crew, farm staff, habitat residents, and intact hull state.
    bool submit_host_colony_departure(const Command& command,
                                      FailPoint fail=FailPoint::None);
    void step_with_colony_departure(FailPoint fail=FailPoint::None);

    // q6e3b: only the trusted host can queue a rescue from a child-occupied
    // ferry. The paired atomic effect MUST execute the due command.
    bool submit_host_dependent_rescue_action(const Command& command,
                                             FailPoint fail=FailPoint::None);
    // Same canonical world transaction; separate public host-only medical entry point.
    bool submit_host_medical_action(const Command& command, FailPoint fail = FailPoint::None);
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
    // TRUSTED HOST only; executes one due terminal ferry incident and commits
    // world snapshot, ART02 deaths, ECON03 population and receipts atomically.
    // For other ticks call step(); non-enrolled regions are refused.
    void step_with_atomic_society(const SocietyAtomicPlan& plan,
                                  FailPoint fail=FailPoint::None);
    // Host-only atomic ART02 reserved dependent birth + ECON03 population
    // increase, journaled on one otherwise command-free world simulation step.
    // Does not add playable NPC or certify reproductive/housing policies.
    void step_with_atomic_dependent_birth(const SocietyBirthPlan& plan,
                                          FailPoint fail=FailPoint::None);
    void step_with_atomic_migration(const SocietyMigrationPlan& plan,
                                    FailPoint fail=FailPoint::None);
    // Host executes exactly one pending real boarding/disembarkation command,
    // atomically accounting for each physical ART02 dependent berth.
    void step_with_dependent_transport(const DependentTravelPlan& plan,
                                       FailPoint fail=FailPoint::None);
    // Moves an entire child manifest to a real receiving ferry atomically
    // with HostEvacuatePassengers. Never invents fatalities or orphaned minors.
    void step_with_dependent_rescue(const DependentRescuePlan& plan,
                                    FailPoint fail=FailPoint::None);
    std::vector<DependentTravelRecord> dependent_travel(const std::string& family_scope) const;
    // Caller provides the COMPLETE manifest expected for the original voyage.
    // Rejects missing/extra/duplicate IDs and broken rescue chains. Never
    // changes custody, advances time or infers casualties from missing data.
    DependentIncidentAudit audit_dependent_incident(
        const std::string& family_scope, std::uint64_t original_transport,
        const std::vector<std::uint64_t>& expected_children) const;
    // Host-only durable case attestation; requires a real already-committed
    // stranding and the COMPLETE manifest. No world population or physical
    // state is changed. Insertion and complete roster are atomic in SQLite.
    void record_dependent_stranding_case(const DependentStrandingCasePlan& plan,
                                         FailPoint fail=FailPoint::None);
    // A case may be adjudicated as fully rescued only immediately following
    // the actual applied evacuation world step. One SQLite transaction binds
    // every dependent to the preexisting case and real receiving parent.
    void record_dependent_rescue_outcome(const DependentRescueOutcomePlan& plan,
                                         FailPoint fail=FailPoint::None);
    DependentRescueOutcome verify_dependent_rescue_outcome(
        const std::string& outcome_id) const;
    // Authoritative *legal* guardianship follows separately verified nonfatal
    // rescue. A non-parent must have a REAL receiving-ferry passenger record.
    // Two-parent children require independent recorded approvals by BOTH.
    void record_nonparent_guardian_approval(const NonParentGuardianApprovalPlan& plan,
                                            FailPoint fail=FailPoint::None);
    void settle_nonparent_guardianship(const NonParentGuardianshipPlan& plan,
                                      FailPoint fail=FailPoint::None);
    VerifiedNonParentGuardianship verify_nonparent_guardianship(
        const std::string& transfer_id) const;
    // Authoritative custody changes only with separate valid legal transfer;
    // requires current ferry co-location, complete minor roster and rollback.
    void transfer_dependent_physical_custody(const DependentPhysicalHandoffPlan& plan,
                                             FailPoint fail=FailPoint::None);
    VerifiedDependentPhysicalHandoff verify_dependent_physical_custody(
        const std::string& handoff_id) const;
    void step_with_multi_dependent_transport(const MultiDependentTransportPlan& plan,
                                              FailPoint fail=FailPoint::None);
    void step_with_multi_dependent_rescue(const MultiDependentRescuePlan& plan,
                                           FailPoint fail=FailPoint::None);
    // 05q6e5b5a: authenticated ALLOCATION INTENT only. Does NOT physically
    // evacuate anyone. Execution requires a distinct atomic two-receiver
    // command which is deliberately not yet exposed to the gameplay runtime.
    void record_split_rescue_intent(const SplitRescueIntentPlan& plan,
                                    FailPoint fail=FailPoint::None);
    VerifiedSplitRescueIntent verify_split_rescue_intent(const std::string& id) const;
    bool submit_host_atomic_split_rescue(const SplitRescueIntentPlan& plan,
        std::uint64_t command_id,FailPoint fail=FailPoint::None,
        int operating_crew_to_first=0);
    void step_with_atomic_split_rescue(const SplitRescueIntentPlan& plan,
        std::uint64_t command_id,FailPoint fail=FailPoint::None);
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
    // Records an explicitly assessed actual physical incident by a *firsthand*
    // witness. Its direct-observation receipt must be from the current tick;
    // hearsay and invented incidents cannot act as physical proof. Group context
    // is optional, but a group member must belong there at this exact epoch.
    bool appraise_witnessed_incident(std::uint64_t observation_id,
          std::uint64_t context_group,PhysicalAppraisalStance stance,int confidence);
    std::vector<PhysicalAppraisalEvent> recent_physical_appraisals(std::size_t limit=50) const;
    // Host-only evidence receipt, never an accusation. Victim is permitted
    // solely on an actual NpcLost incident naming the NPC; FirsthandObserver
    // solely on a real current-tick direct-observation knowledge receipt.
    // There is intentionally no NPC perpetrator/beneficiary role yet.
    bool attest_incident_role(const std::string& incident_id,std::uint64_t npc_id,
            VerifiedNpcRole role,std::uint64_t observation_id=0);
    bool is_verified_incident_role(const std::string& incident_id,
            std::uint64_t npc_id,VerifiedNpcRole role) const;
    std::vector<IncidentRoleReceipt> recent_incident_roles(std::size_t limit=50) const;
    // GRSM-04b.B1: Only this explicit host-owned execution route may name a
    // worker. It accepts a real Mine command; a completion is recorded later,
    // atomically with the executed command's actual ore-transfer incident.
    // Merely sharing a ship or witnessing a mine is never execution proof.
    bool submit_npc_mining_work(std::uint64_t npc_id, EntityId ship,EntityId rock,
          std::int64_t amount,std::uint64_t command_id,FailPoint fail=FailPoint::None);
    // Named, host-attested fabrication using existing real ship machinery,
    // not a synthetic warehouse or free-standing workbench inventory.
    bool submit_npc_fabrication_work(std::uint64_t npc_id,EntityId ship,
        Action action,EntityId material_code,std::int64_t quantity,
        std::uint64_t command_id,FailPoint fail=FailPoint::None);
    bool verified_npc_fabrication_execution(std::uint64_t command_id,std::uint64_t npc_id) const;
    std::vector<NpcFabricationAssignment> recent_npc_fabrication_assignments(std::size_t limit=50) const;
    std::vector<NpcFabricationExecution> recent_npc_fabrication_executions(std::size_t limit=50) const;
    bool verified_npc_mining_execution(std::uint64_t command_id,std::uint64_t npc_id) const;
    std::vector<NpcMiningAssignment> recent_npc_mining_assignments(std::size_t limit=50) const;
    std::vector<NpcMiningExecution> recent_npc_mining_executions(std::size_t limit=50) const;







private:
    // Only the trusted regional production bridge may attach a physical
    // inventory escrow effect to this canonical world command.
    void settle_constructed_stock(const Command&,
        const std::function<void(sqlite3*,std::uint64_t)>&,FailPoint);
    class Store;
    std::unique_ptr<Store> store_;
    std::string db_path_;
    Simulation world_;
    std::int64_t resource_origin_ = 0;
    std::int64_t credit_origin_ = 0;
    std::uint64_t generation_ = 0;
};
} // namespace galaxy
