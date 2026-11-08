#pragma once
#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <string>
#include <sqlite3.h>

namespace galaxy::fp3d {
// FP3D-02C prototype: a native HOST-ONLY on-foot pose in the registered
// player's own ship interior. XYZ millimeters, Y=0 floor. This is NOT yet a
// disembark, networking, Godot movement, or remote actor-authorization API.
enum class MoveFailPoint { None, AfterStepReceipt, AfterPoseUpdate };

struct Pose {
    PlayerId owner=0;
    EntityId ship=0;
    std::int64_t x_mm=0,y_mm=0,z_mm=0;
    std::uint64_t sequence=0,world_tick=0,world_generation=0;
    std::string digest;
};

class InteriorMovement {
public:
    // Must be called by the trusted *same-process host* holding the original
    // verified PersistentWorld. No browser or Godot writes SQLite directly.
    InteriorMovement(const std::string& sqlite_path,const PersistentWorld& world);
    ~InteriorMovement();
    InteriorMovement(const InteriorMovement&)=delete;
    InteriorMovement& operator=(const InteriorMovement&)=delete;
    // Creates the origin record only when the original native persona is
    // registered, aboard their OWN original hull, and the snapshot is current.
    Pose current(PlayerId trusted_owner);
    // One <=100mm planar step per actual native simulation tick. Expected
    // sequence and unique receipt prevent replay and buffered command floods.
    // Input direction is constrained by world geometry; no diagonal speed gain.
    bool move(PlayerId trusted_owner,std::uint64_t expected_sequence,
              std::uint64_t receipt_id,std::int64_t dx_mm,std::int64_t dz_mm,
              MoveFailPoint fault=MoveFailPoint::None);
private:
    sqlite3* db_=nullptr;
    const PersistentWorld& world_;
    bool valid_person(PlayerId owner,EntityId ship)const;
    void verify_world_generation()const;
    // Each movement must be bound to a real committed native kind=2 STEP,
    // with the original snapshot checksum/tick, not a synthetic counter.
    void verify_step_receipt(std::uint64_t generation,std::uint64_t tick)const;
    Pose load_or_origin(PlayerId owner);
    void verify_chain(const Pose& pose);
};
} // namespace galaxy::fp3d
