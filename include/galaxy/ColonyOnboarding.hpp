#pragma once
// DOCK-09: ORIGINAL physical colony-ship build path. Intent proposals and
// source observations only; never generate an outpost, free module, or good
// in an alternative Godot or SQLite ledger.
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
namespace galaxy::colony_onboarding {

enum class Operation : unsigned { RefineRawOre=1, MakeModules=2,
                                   EstablishOutpost=3, SeparateAluminum=4 };

inline std::optional<Command> propose(const Simulation& sim,
    PlayerId owner,EntityId ship,std::uint64_t command_id,Operation selected) {
    if(!command_id || command_id>static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) ||
       sim.has_command_id(command_id) ||
       sim.tick()==std::numeric_limits<std::uint64_t>::max())
       return std::nullopt;
    const auto* hull=sim.find(ship);
    const auto* cargo=sim.colony_ship(ship);
    if(!owner || !hull || hull->type!=EntityType::Ship ||
       hull->owner_id!=owner || !cargo ||
       hull->jump_state!=ShipJumpState::Ready ||
       cargo->crew<2 ||
       (hull->dock_state!=DockState::Docked &&
        hull->dock_state!=DockState::InSpace))
        return std::nullopt;
    // The caller also verifies the ORIGINAL seated player and seat
    // revision. The original kernel performs full space/dock, site overlap,
    // cargo provenance, crew and world authority validation after submit.
    Command c{};
    c.id=command_id;c.sequence=c.id;c.issuer=owner;c.actor=ship;
    c.execute_tick=sim.tick()+1;
    c.amount=1;
    switch(selected) {
      case Operation::RefineRawOre:
        if(hull->cargo<1)return std::nullopt;
        c.action=Action::RefineOre;
        c.amount=std::min<std::int64_t>(4,hull->cargo);
        break;
      case Operation::MakeModules:
        if(cargo->refined<2)return std::nullopt;
        c.action=Action::FabricateModule;
        c.amount=std::min<std::int64_t>(2,cargo->refined/2);
        break;
      case Operation::EstablishOutpost:
        if(cargo->crew<3 || cargo->modules<4 ||
           hull->dock_state!=DockState::InSpace ||
           hull->velocity.x || hull->velocity.y ||
           hull->velocity_z_mm_per_tick)
            return std::nullopt;
        c.action=Action::ConstructOutpost;c.amount=1;
        break;
      case Operation::SeparateAluminum: {
        const auto mineral=cargo->minerals_kg.find("plagioclase");
        if(mineral==cargo->minerals_kg.end() || mineral->second<1)
            return std::nullopt;
        // Code 10 is the ORIGINAL plagioclase recipe, whose material
        // balances the native Simulation itself owns.
        c.action=Action::RefineMineral;c.target=10;
        c.amount=std::min<std::int64_t>(12000,mineral->second);
        break;
      }
      default:return std::nullopt;
    }
    return c;
}

inline EntityId constructed_site(const Simulation& sim,PlayerId owner,
                                 EntityId ship) {
    EntityId latest=0;
    for(const auto& [station,outpost]:sim.built_outposts()){
        if(outpost.source_colony_ship!=ship)continue;
        const auto* entity=sim.find(station);
        if(entity && entity->type==EntityType::Station &&
           entity->owner_id==owner)latest=std::max(latest,station);
    }
    return latest;
}

inline std::string source_frame(const PersistentWorld& persistent,
                                PlayerId owner,EntityId ship,
                                std::uint64_t session,std::uint64_t request) {
    const auto& sim=persistent.world();
    const auto* hull=sim.find(ship);
    const auto* cargo=sim.colony_ship(ship);
    if(!hull || hull->type!=EntityType::Ship || hull->owner_id!=owner ||
       !cargo)throw std::runtime_error("invalid original colony build source");
    const auto plagioclase=cargo->minerals_kg.find("plagioclase");
    const auto aluminum=cargo->goods_kg.find("aluminum");
    const auto site=constructed_site(sim,owner,ship);
    // A fixed-size read-only frame; all amounts retain their original units.
    std::ostringstream o;
    o<<"ONBOARD|1|"<<session<<'|'<<request<<'|'<<ship
     <<'|'<<sim.tick()<<'|'<<persistent.generation()
     <<'|'<<hull->cargo<<'|'<<cargo->refined<<'|'<<cargo->modules
     <<'|'<<(plagioclase==cargo->minerals_kg.end()?0:plagioclase->second)
     <<'|'<<(aluminum==cargo->goods_kg.end()?0:aluminum->second)
     <<'|'<<site;
    for(unsigned i=1;i<=4;++i)
        o<<'|'<<(propose(sim,owner,ship,0x7000000000000000ULL+i,
                         static_cast<Operation>(i))?1:0);
    o<<'\n';
    return o.str();
}
} // namespace galaxy::colony_onboarding
