#pragma once
// PLAY-13D.4C.1: standalone, finite shuttle custody domain.
// Not a host command, SQLite record, or simulated flight scheduler.
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace galaxy::planetary {
using ShuttleId = std::uint64_t;
using NpcId = std::uint64_t;
using HostId = std::uint64_t;
using ShuttleRegionKey = std::tuple<std::uint64_t,std::uint16_t,std::uint16_t,std::uint32_t>;
enum class ShuttleMaterial : std::uint8_t {
  Water, Nutrients, Food, Oxygen, Co2, Wastewater, OrganicWaste,
  Compost, StructuralMaterial, Fuel, Count
};
constexpr std::size_t shuttle_material_count = static_cast<std::size_t>(ShuttleMaterial::Count);
using ShuttleStock = std::array<std::int64_t,shuttle_material_count>;

inline std::int64_t checked_shuttle_mass(const ShuttleStock& stock) {
  std::int64_t total=0;
  for(const auto kg:stock) {
    if(kg<0 || kg>std::numeric_limits<std::int64_t>::max()-total)
      throw std::invalid_argument("negative or overflowing shuttle mass");
    total+=kg;
  }
  return total;
}
enum class ShuttleBerth : std::uint8_t { Orbit, Surface, InFlight };
struct ShuttleLocation {
  ShuttleBerth berth=ShuttleBerth::Orbit;
  HostId ship=0;
  ShuttleRegionKey region{};
};
struct ShuttleVehicle {
  ShuttleId id=0;
  std::uint64_t owner=0;
  HostId ship=0;
  ShuttleRegionKey region{};
  std::int64_t dry_mass_kg=0;
  std::int64_t cargo_limit_kg=0;
  std::uint32_t seat_limit=0;
  std::int64_t fuel_limit_kg=0;
  std::uint8_t condition=100;
  ShuttleBerth berth=ShuttleBerth::Orbit;
  ShuttleStock manifest{};
  std::vector<NpcId> passengers;
  std::int64_t fuel_kg=0;
};

class ShuttleCustody {
public:
  // Genesis/host-import only. 4C.2 must make these operations unavailable to players.
  void seed_ship(HostId ship, const ShuttleStock& stock) {
    if(!ship || ships_.count(ship))throw std::invalid_argument("duplicate or zero ship");
    checked_shuttle_mass(stock);
    ships_.emplace(ship,stock);
  }
  void seed_surface(ShuttleRegionKey region,const ShuttleStock& stock) {
    if(!std::get<0>(region) || !std::get<3>(region) || surfaces_.count(region))
      throw std::invalid_argument("invalid or duplicate physical region");
    checked_shuttle_mass(stock);
    surfaces_.emplace(region,stock);
  }
  void seed_npc(NpcId npc, ShuttleLocation where) {
    if(!npc || npcs_.count(npc) || !valid_location(where))
      throw std::invalid_argument("invalid or duplicate NPC");
    npcs_.emplace(npc,where);
  }
  void register_shuttle(ShuttleVehicle shuttle) {
    if(!shuttle.id || !shuttle.owner || !shuttle.ship ||
       !std::get<0>(shuttle.region) || !std::get<3>(shuttle.region) ||
       shuttle.dry_mass_kg<=0 || shuttle.cargo_limit_kg<=0 ||
       !shuttle.seat_limit || shuttle.fuel_limit_kg<0 ||
       shuttle.fuel_kg<0 || shuttle.fuel_kg>shuttle.fuel_limit_kg ||
       shuttle.condition>100 || shuttle.berth!=ShuttleBerth::Orbit ||
       !ships_.count(shuttle.ship) || !surfaces_.count(shuttle.region) ||
       shuttles_.count(shuttle.id) || !shuttle.passengers.empty() ||
       checked_shuttle_mass(shuttle.manifest)!=0)
      throw std::invalid_argument("invalid new shuttle");
    shuttles_.emplace(shuttle.id,std::move(shuttle));
  }
  const ShuttleVehicle& shuttle(ShuttleId id) const {return shuttles_.at(id);}
  const ShuttleStock& ship_stock(HostId id) const {return ships_.at(id);}
  const ShuttleStock& surface_stock(ShuttleRegionKey id) const {return surfaces_.at(id);}
  ShuttleLocation npc_location(NpcId id) const {return npcs_.at(id);}

  // Explicit physical origin/destination. All checks happen before mutation.
  // No movement while in flight; flight transitions belong to 4C.3.
  bool load(ShuttleId id, const ShuttleStock& requested, const std::vector<NpcId>& boarding) {
    auto it=shuttles_.find(id);
    if(it==shuttles_.end())return false;
    auto& v=it->second;
    if(v.berth==ShuttleBerth::InFlight)return false;
    const auto mass=checked_shuttle_mass(requested);
    if(mass>v.cargo_limit_kg-checked_shuttle_mass(v.manifest) ||
       boarding.size()>v.seat_limit-v.passengers.size())return false;
    ShuttleStock* origin=(v.berth==ShuttleBerth::Orbit)?
      &ships_.at(v.ship):&surfaces_.at(v.region);
    for(std::size_t i=0;i<shuttle_material_count;++i)
      if(requested[i]>(*origin)[i])return false;
    std::set<NpcId> unique(v.passengers.begin(),v.passengers.end());
    const ShuttleLocation expected{v.berth,v.ship,v.region};
    for(const auto npc:boarding) {
      const auto found=npcs_.find(npc);
      if(!unique.insert(npc).second || found==npcs_.end() ||
         found->second.berth!=expected.berth ||
         found->second.ship!=expected.ship ||
         (expected.berth==ShuttleBerth::Surface && found->second.region!=expected.region))
        return false;
    }
    for(std::size_t i=0;i<shuttle_material_count;++i) {
      (*origin)[i]-=requested[i];
      v.manifest[i]+=requested[i];
    }
    for(const auto npc:boarding) {
      v.passengers.push_back(npc);
      npcs_.at(npc)=ShuttleLocation{ShuttleBerth::InFlight,v.ship,v.region};
    }
    return true;
  }
  // Unload at current berth, no remote delivery. 4C.3 will control changes of berth.
  bool unload(ShuttleId id,const ShuttleStock& requested,const std::vector<NpcId>& leaving) {
    auto it=shuttles_.find(id);
    if(it==shuttles_.end() || it->second.berth==ShuttleBerth::InFlight)return false;
    auto& v=it->second;
    checked_shuttle_mass(requested);
    for(std::size_t i=0;i<shuttle_material_count;++i)
      if(requested[i]>v.manifest[i])return false;
    std::set<NpcId> unique;
    for(auto npc:leaving)
      if(!unique.insert(npc).second ||
         std::find(v.passengers.begin(),v.passengers.end(),npc)==v.passengers.end())
        return false;
    ShuttleStock* destination=(v.berth==ShuttleBerth::Orbit)?
      &ships_.at(v.ship):&surfaces_.at(v.region);
    for(std::size_t i=0;i<shuttle_material_count;++i)
      if(requested[i]>std::numeric_limits<std::int64_t>::max()-(*destination)[i])return false;
    for(std::size_t i=0;i<shuttle_material_count;++i) {
      v.manifest[i]-=requested[i];
      (*destination)[i]+=requested[i];
    }
    for(auto npc:leaving) {
      auto pos=std::find(v.passengers.begin(),v.passengers.end(),npc);
      v.passengers.erase(pos);
      npcs_.at(npc)=ShuttleLocation{v.berth,v.ship,v.region};
    }
    return true;
  }
  // This domain-only hook simulates an externally verified arrival; production
  // must never expose it directly. 4C.3 replaces it with committed flight transitions.
  bool test_arrive(ShuttleId id,ShuttleBerth destination) {
    auto it=shuttles_.find(id);
    if(it==shuttles_.end() || destination==ShuttleBerth::InFlight ||
       it->second.berth==destination)return false;
    it->second.berth=destination;
    return true;
  }
  std::int64_t total_mobile_mass() const {
    std::int64_t total=0;
    auto add=[&](std::int64_t n){
      if(n<0 || n>std::numeric_limits<std::int64_t>::max()-total)
        throw std::overflow_error("shuttle world mass overflow");
      total+=n;
    };
    for(const auto& p:ships_)add(checked_shuttle_mass(p.second));
    for(const auto& p:surfaces_)add(checked_shuttle_mass(p.second));
    for(const auto& p:shuttles_) {
      add(p.second.dry_mass_kg);add(p.second.fuel_kg);
      add(checked_shuttle_mass(p.second.manifest));
    }
    return total;
  }
  void validate() const {
    for(const auto& [id,v]:shuttles_) {
      if(id!=v.id || !ships_.count(v.ship) || !surfaces_.count(v.region) ||
         v.passengers.size()>v.seat_limit ||
         checked_shuttle_mass(v.manifest)>v.cargo_limit_kg ||
         v.fuel_kg<0 || v.fuel_kg>v.fuel_limit_kg ||
         v.dry_mass_kg<=0)throw std::logic_error("invalid shuttle inventory");
      std::set<NpcId> unique;
      for(auto npc:v.passengers) {
        if(!unique.insert(npc).second || !npcs_.count(npc) ||
           npcs_.at(npc).berth!=ShuttleBerth::InFlight ||
           npcs_.at(npc).ship!=v.ship || npcs_.at(npc).region!=v.region)
          throw std::logic_error("NPC shuttle custody mismatch");
      }
    }
    for(const auto& [id,loc]:npcs_) {
      if(!valid_location(loc))throw std::logic_error("orphan NPC custody");
      if(loc.berth==ShuttleBerth::InFlight) {
        std::size_t count=0;
        for(const auto& [sid,v]:shuttles_) {
          (void)sid;
          for(auto passenger:v.passengers)if(passenger==id)++count;
        }
        if(count!=1)throw std::logic_error("NPC aboard zero or multiple shuttles");
      }
    }
    (void)total_mobile_mass();
  }
private:
  bool valid_location(const ShuttleLocation& where) const {
    if(where.berth==ShuttleBerth::Orbit)return ships_.count(where.ship)!=0;
    if(where.berth==ShuttleBerth::Surface)
      return ships_.count(where.ship)!=0 && surfaces_.count(where.region)!=0;
    return false; // flight custody is only obtained by boarding an existing shuttle
  }
  std::map<HostId,ShuttleStock> ships_;
  std::map<ShuttleRegionKey,ShuttleStock> surfaces_;
  std::map<ShuttleId,ShuttleVehicle> shuttles_;
  std::map<NpcId,ShuttleLocation> npcs_;
};
} // namespace galaxy::planetary
