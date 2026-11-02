#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct sqlite3;

namespace galaxy {

// Visual inheritance only. No genes/traits for intelligence, personality, culture,
// occupation, health, morality or ability. This is deliberately not biological DNA.
enum class FaceTrait : std::size_t {
    HeadWidth, FaceLength, ForeheadHeight, BrowWeight, BrowArch,
    EyeSpacing, EyeShape, NoseBridge, NoseWidth, NoseProjection,
    CheekboneHeight, ChinDepth, JawWidth, MouthWidth, LipFullness,
    EarSize, SkinPigment, EyePigment, HairPigment, HairCurl, Count
};

constexpr std::size_t kFaceTraitCount = static_cast<std::size_t>(FaceTrait::Count);
constexpr std::size_t kFaceLociPerTrait = 4;
constexpr std::size_t kFaceAlleles = kFaceTraitCount * kFaceLociPerTrait * 2;
constexpr std::uint32_t kFaceGenomeVersion = 1;
static_assert(kFaceAlleles == 160, "v1 SQLite profile length must remain stable");

// ART-02: tracked birth increments one settlement cohort's census exactly once.
// These are named identities in a *family census*, not yet playable Simulation NPCs.
struct NpcFamilyCensus {
    std::uint64_t settlement=0;
    std::uint64_t initial_population=0, current_population=0;
    std::uint64_t revision=0, last_tick=0;
};
struct NpcBirthRecord {
    std::uint64_t event_id=0, child_id=0, parent_a=0, parent_b=0;
    std::uint64_t settlement=0, tick=0, census_revision=0;
    std::uint64_t appearance_digest=0;
};

struct NpcDeathRecord {
    std::uint64_t event_id=0,npc_id=0,settlement=0,tick=0,census_revision=0;
};

struct NpcAppearanceProfile {
    std::uint64_t npc_id=0;
    std::uint64_t parent_a=0, parent_b=0;  // 0,0 for unrelated founders
    std::uint32_t generation=0;
    std::array<std::uint8_t,kFaceAlleles> alleles{};
    std::uint64_t digest=0; // crash/corruption check, NOT a cryptographic signature

    // Fixed, renderer-independent phenotype in [0,1]. Must be mapped to
    // rig-specific and mesh-safe limits by the eventual 3D character renderer.
    std::array<float,kFaceTraitCount> facial_morphs() const noexcept;
};

// Small opt-in SQLite appearance registry for AUTHORITATIVELY EXISTING named NPCs.
// Caller must use a trusted identity admission function. This repository does
// not create births, NPCs, physical population, or renderer/face textures.
// Do not call the admission function from an untrusted client.
class NpcAppearanceStore {
public:
    using AdmitExistingNpc = std::function<bool(std::uint64_t)>;

    NpcAppearanceStore(const std::string& sqlite_path, std::string scope,
                       std::uint64_t world_seed, AdmitExistingNpc admit);
    // Trusted PersistentWorld::Store::commit use ONLY. Does not own the db handle
    // or open/commit/rollback transactions; validates pre-existing metadata.
    NpcAppearanceStore(sqlite3* active_world_transaction, std::string scope,
                       std::uint64_t world_seed, AdmitExistingNpc admit);
    ~NpcAppearanceStore();
    NpcAppearanceStore(const NpcAppearanceStore&) = delete;
    NpcAppearanceStore& operator=(const NpcAppearanceStore&) = delete;

    NpcAppearanceProfile ensure_founder(std::uint64_t npc_id);
    NpcAppearanceProfile ensure_child(std::uint64_t npc_id,
                                      std::uint64_t parent_a, std::uint64_t parent_b);
    bool exists(std::uint64_t npc_id) const;
    NpcAppearanceProfile get(std::uint64_t npc_id) const;
    std::uint64_t named_profile_count() const;

    // Host-authorized, opt-in population ledger. The population baseline must
    // be provided by the authoritative settlement census; it is not inferred
    // from named face profiles. Births increment the census but do NOT create
    // a playable Simulation::NpcRecord until the later world migration gate.
    void register_settlement(std::uint64_t settlement, std::uint64_t population);
    NpcFamilyCensus family_census(std::uint64_t settlement) const;
    NpcAppearanceProfile enroll_family_founder(std::uint64_t npc_id,
                                                std::uint64_t settlement);
    // Child IDs live in a reserved, non-playable namespace (high bit range).
    // authorize_birth is a trusted host policy gate; it must check resources,
    // housing, demographics, etc. This ledger cannot prove those by itself.
    using AuthorizeBirth = std::function<bool(std::uint64_t settlement,
                                               std::uint64_t parent_a,
                                               std::uint64_t parent_b,
                                               std::uint64_t tick)>;
    NpcBirthRecord record_birth(std::uint64_t event_id, std::uint64_t child_id,
                                std::uint64_t parent_a, std::uint64_t parent_b,
                                std::uint64_t settlement, std::uint64_t tick,
                                std::uint64_t expected_census_revision,
                                AuthorizeBirth authorize_birth);
    NpcBirthRecord get_birth(std::uint64_t event_id) const;
    // Only legal when born low-ID is already in an authoritative cloned
    // Simulation and the world snapshot and this receipt commit together.
    NpcBirthRecord record_live_birth(std::uint64_t event_id,std::uint64_t child_id,
                                std::uint64_t parent_a,std::uint64_t parent_b,
                                std::uint64_t settlement,std::uint64_t tick,
                                std::uint64_t expected_revision,AuthorizeBirth authorize_birth);
    using AuthorizeDeath = std::function<bool(std::uint64_t settlement,
                                              std::uint64_t npc_id,
                                              std::uint64_t tick)>;
    NpcDeathRecord record_death(std::uint64_t event_id,std::uint64_t npc_id,
                                std::uint64_t settlement,std::uint64_t tick,
                                std::uint64_t expected_census_revision,
                                AuthorizeDeath authorize_death);
    NpcDeathRecord get_death(std::uint64_t event_id) const;

private:
    NpcBirthRecord record_birth_impl(std::uint64_t event_id,std::uint64_t child_id,
                        std::uint64_t parent_a,std::uint64_t parent_b,
                        std::uint64_t settlement,std::uint64_t tick,
                        std::uint64_t expected_revision,AuthorizeBirth authorize_birth,
                        bool world_live_identity);
    NpcAppearanceProfile lookup(std::uint64_t id, bool& found) const;
    void insert(const NpcAppearanceProfile& profile);
    void begin();
    void commit();
    void rollback() noexcept;
    void require_existing_npc(std::uint64_t id) const;
    bool is_born_npc(std::uint64_t id) const;
    sqlite3* db_=nullptr;
    bool borrowed_transaction_=false;
    std::string scope_;
    std::uint64_t seed_=0;
    AdmitExistingNpc admit_;
};

} // namespace galaxy
