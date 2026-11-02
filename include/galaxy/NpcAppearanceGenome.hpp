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
    ~NpcAppearanceStore();
    NpcAppearanceStore(const NpcAppearanceStore&) = delete;
    NpcAppearanceStore& operator=(const NpcAppearanceStore&) = delete;

    NpcAppearanceProfile ensure_founder(std::uint64_t npc_id);
    NpcAppearanceProfile ensure_child(std::uint64_t npc_id,
                                      std::uint64_t parent_a, std::uint64_t parent_b);
    bool exists(std::uint64_t npc_id) const;
    NpcAppearanceProfile get(std::uint64_t npc_id) const;
    std::uint64_t named_profile_count() const;

private:
    NpcAppearanceProfile lookup(std::uint64_t id, bool& found) const;
    void insert(const NpcAppearanceProfile& profile);
    void begin();
    void commit();
    void rollback() noexcept;
    void require_existing_npc(std::uint64_t id) const;
    sqlite3* db_=nullptr;
    std::string scope_;
    std::uint64_t seed_=0;
    AdmitExistingNpc admit_;
};

} // namespace galaxy
