#pragma once
#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <string>
#include <optional>
#include <sqlite3.h>

namespace galaxy::fp3d {
// FP3D-02C prototype: a native HOST-ONLY on-foot pose in the registered
// player's own ship interior. XYZ millimeters, Y=0 floor. This is NOT yet a
// disembark, networking, Godot movement, or remote actor-authorization API.
enum class MoveFailPoint { None, AfterStepReceipt, AfterPoseUpdate };
enum class SeatFailPoint { None, AfterSeatReceipt, AfterSeatUpdate };
// Ship-local acceleration uses the ORIGINAL 2D yaw/velocity integrator.
// Yaw is 15 degrees per accepted command; port/starboard are translational
// strafe, NOT fictional pitch. Vertical pitch/roll remain separate gates.
enum class PilotFailPoint { None, AfterAcceptance };
enum class PilotControl { Undock, YawLeft, YawRight, Forward, Reverse,
                          StrafePort, StrafeStarboard, Counterthrust };
struct FlightState {
    EntityId ship=0;
    std::uint64_t tick=0,generation=0;
    std::int64_t x=0,y=0,z=0,vx=0,vy=0,vz=0;
    std::uint16_t heading=0;
    DockState dock=DockState::Docked;
};

struct Pose {
    PlayerId owner=0;
    EntityId ship=0;
    std::int64_t x_mm=0,y_mm=0,z_mm=0;
    std::uint64_t sequence=0,world_tick=0,world_generation=0;
    std::string digest;
    bool seated=false; // host-journaled cockpit state, NOT ship propulsion or disembarkation
    std::uint64_t seat_sequence=0;
};

class InteriorMovement {
public:
    // Must be called by the trusted *same-process host* holding the original
    // verified PersistentWorld. No browser or Godot writes SQLite directly.
    InteriorMovement(const std::string& sqlite_path,PersistentWorld& world);
    ~InteriorMovement();
    InteriorMovement(const InteriorMovement&)=delete;
    InteriorMovement& operator=(const InteriorMovement&)=delete;
    // Creates the origin record only when the original native persona is
    // registered, aboard their OWN original hull, and the snapshot is current.
    Pose current(PlayerId trusted_owner);
    // Atomic path: one <=100mm planar input step advances one authoritative world tick
    // and writes movement pose/receipt in the SAME SQLite BEGIN IMMEDIATE/COMMIT
    // as operation_log STEP and canonical snapshot. No second SQLite writer.
    // Expected sequence and unique receipt prevent replay and command floods.
    // Input direction is constrained by world geometry; no diagonal speed gain.
    bool move(PlayerId trusted_owner,std::uint64_t expected_sequence,
              std::uint64_t receipt_id,std::int64_t dx_mm,std::int64_t dz_mm,
              MoveFailPoint fault=MoveFailPoint::None);
    // Host-only transition at the ship's native cockpit interaction zone.
    // Accepted transition commits original STEP + immutable receipt + seat state
    // together; a seated person cannot issue walking MOVE commands.
    bool set_seated(PlayerId trusted_owner,std::uint64_t expected_seat_sequence,
                    std::uint64_t receipt_id,bool want_seated,
                    SeatFailPoint fault=SeatFailPoint::None);
    // Seat-checked original flight commands. No local vehicle-physics clone.
    // Command acceptance and execution are separate canonical journal commits;
    // an interrupted pending command blocks future host sessions until repaired.
    std::optional<Outcome> pilot(PlayerId trusted_owner,
        std::uint64_t expected_seat_sequence,std::uint64_t command_id,
        PilotControl control,PilotFailPoint fault=PilotFailPoint::None);
    FlightState flight_state(PlayerId trusted_owner);

private:
    sqlite3* db_=nullptr;
    PersistentWorld& world_;
    bool valid_person(PlayerId owner,EntityId ship)const;
    void verify_world_generation()const;
    // Each movement must be bound to a real committed native kind=2 STEP,
    // with the original snapshot checksum/tick, not a synthetic counter.
    void verify_step_receipt(std::uint64_t generation,std::uint64_t tick)const;
    Pose load_or_origin(PlayerId owner);
    void verify_chain(const Pose& pose);
    void load_seat(Pose& pose);
    void verify_seat_chain(const Pose& pose)const;
};
} // namespace galaxy::fp3d
