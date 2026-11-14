#pragma once
// PLAY-13D universal goods transport contract.
// A transport can carry named NPCs, finite resources and later equipment.
// This is an immutable projection/order schema, NOT a cargo ledger, scheduler
// or permission to move stock. Vehicle adapters read the authoritative world
// and perform actual transfers under their existing host/SQLite controls.
#include "galaxy/TransportIdentity.hpp"
#include <cstdint>
#include <limits>
#include <vector>

namespace galaxy::transport {
inline bool valid_transport_endpoint(Endpoint e) noexcept {
    const auto kind=static_cast<std::uint8_t>(e.kind);
    return e.id && kind>=1 && kind<=4;
}
enum class GoodKind : std::uint8_t { Npc=1, Resource=2, Equipment=3 };
enum class ResourceGood : std::uint8_t {
    Water=1, Food=2, Nutrients=3, Oxygen=4,
    PlanetarySoil=5, PlanetaryMineral=6, PlanetaryVolatile=7
};
// WO3-B: one actually fabricated 2-tonne construction module on the
// existing planetary shuttle's external rack, not generic cargo kilograms.
inline constexpr std::uint64_t kPlanetaryHousingModuleEquipmentId=1;
struct Good {
    GoodKind kind=GoodKind::Resource;
    std::uint64_t id=0;       // named NPC id, resource id, or physical equipment id
    std::uint64_t amount=0;   // NPC: one, resource: kilograms, equipment: pieces
    std::uint64_t mass_kg=0;  // cargo hold mass; NPCs occupy seats, not bulk cargo
};
inline bool valid_good(const Good& g) noexcept {
    if(!g.id || !g.amount)return false;
    switch(g.kind){
    case GoodKind::Npc: return g.amount==1 && g.mass_kg==0;
    case GoodKind::Resource:
        return g.id>=static_cast<std::uint8_t>(ResourceGood::Water) &&
               g.id<=static_cast<std::uint8_t>(ResourceGood::PlanetaryVolatile) &&
               g.mass_kg==g.amount;
    case GoodKind::Equipment:
        // Generic future equipment support requires the host to supply an
        // actual physical inventory id AND nonzero total mass per line.
        return g.mass_kg>=g.amount;
    }
    return false;
}
inline bool same_good_key(const Good& a,const Good& b) noexcept {
    return a.kind==b.kind && a.id==b.id;
}
inline bool before_good_key(const Good& a,const Good& b) noexcept {
    return a.kind==b.kind ? a.id<b.id :
        static_cast<std::uint8_t>(a.kind)<static_cast<std::uint8_t>(b.kind);
}
inline bool canonical_goods(const std::vector<Good>& goods) noexcept {
    if(goods.empty() || goods.size()>1024)return false;
    for(std::size_t i=0;i<goods.size();++i)
        if(!valid_good(goods[i]) ||
           (i && !before_good_key(goods[i-1],goods[i])))return false;
    return true;
}
struct Manifest {
    std::uint64_t vehicle=0,owner=0;
    Mode mode=Mode::PlanetaryShuttle;
    Endpoint berth{},other_end{};
    std::uint64_t pilot=0,flight_receipt=0;
    bool in_transit=false;
    std::uint64_t seat_capacity=0,cargo_capacity_kg=0;
    std::vector<Good> contents; // projection, never an independent inventory
    bool valid() const noexcept {
        if(!vehicle || !owner || !valid_transport_endpoint(berth) ||
           !valid_transport_endpoint(other_end) ||
           berth==other_end || seat_capacity>10000 ||
           cargo_capacity_kg>1000000000000ULL ||
           static_cast<unsigned>(mode)<1 || static_cast<unsigned>(mode)>4 ||
           (in_transit && !flight_receipt) ||
           contents.size()>1024)return false;
        if(!contents.empty() && !canonical_goods(contents))return false;
        std::uint64_t passengers=0,mass=0;
        for(const auto& item:contents){
            if(!valid_good(item))return false;
            if(item.kind==GoodKind::Npc){
                if(item.id==pilot)return false;
                if(++passengers>seat_capacity)return false;
            }else{
                if(item.mass_kg>cargo_capacity_kg-mass)return false;
                mass+=item.mass_kg;
            }
        }
        return true;
    }
};

// A player-directed pickup/delivery instruction. Even a valid order must be
// authorized and matched to REAL stock/custody by the native vehicle adapter.
// A passenger order never bypasses NPC guardianship or family consent.
struct Instruction {
    std::uint64_t command_id=0,owner=0,vehicle=0;
    Endpoint pickup{},delivery{};
    std::vector<Good> goods;
    bool valid() const noexcept {
        return command_id && owner && vehicle &&
               valid_transport_endpoint(pickup) &&
               valid_transport_endpoint(delivery) &&
               pickup!=delivery && canonical_goods(goods);
    }
};
inline bool serves(const Manifest& vehicle,const Instruction& instruction) noexcept {
    return vehicle.valid() && instruction.valid() &&
           vehicle.vehicle==instruction.vehicle &&
           vehicle.owner==instruction.owner &&
           ((instruction.pickup==vehicle.berth && instruction.delivery==vehicle.other_end) ||
            (instruction.pickup==vehicle.other_end && instruction.delivery==vehicle.berth));
}
inline bool contains_good(const Manifest& manifest,GoodKind kind,
                          std::uint64_t id,std::uint64_t minimum) noexcept {
    if(!manifest.valid() || !minimum)return false;
    for(const auto& item:manifest.contents)
        if(item.kind==kind && item.id==id)
            return item.amount>=minimum;
    return false;
}
} // namespace galaxy::transport
