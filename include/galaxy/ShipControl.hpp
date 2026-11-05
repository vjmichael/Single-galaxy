#pragma once
// Ship control policy overlays the ONE physical ship entity.
// Do not subclass the physical ship or duplicate inventories/physics.
// UserShip and NpcShip are deliberately lightweight control views.
#include <cstdint>
namespace galaxy::ship_control {
using ShipId=std::uint64_t;
using PrincipalId=std::uint64_t;
enum class Controller : std::uint8_t { User=1,Npc=2 };
struct ShipControl {
    ShipId ship=0;
    PrincipalId principal=0;
    Controller controller=Controller::User;
    PrincipalId owner=0;
    constexpr bool valid() const noexcept {
        return ship!=0 && principal!=0 && owner!=0;
    }
    constexpr bool can_issue_player_order(PrincipalId authenticated) const noexcept {
        return valid() && controller==Controller::User &&
               principal==authenticated && owner==authenticated;
    }
    constexpr bool can_run_delegated_npc_order(PrincipalId npc,
                                               bool host_authorized) const noexcept {
        return valid() && controller==Controller::Npc &&
               principal==npc && host_authorized;
    }
};
struct UserShip final {
    ShipControl control;
    constexpr UserShip(ShipId ship,PrincipalId player):
      control{ship,player,Controller::User,player} {}
};
struct NpcShip final {
    ShipControl control;
    constexpr NpcShip(ShipId ship,PrincipalId npc,PrincipalId owner):
      control{ship,npc,Controller::Npc,owner} {}
};
} // namespace galaxy::ship_control
