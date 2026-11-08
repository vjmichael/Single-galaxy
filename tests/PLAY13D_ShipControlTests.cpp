#include "galaxy/ShipControl.hpp"
#include <cassert>
using namespace galaxy::ship_control;
int main(){
    constexpr UserShip user{123,7};
    constexpr NpcShip npc{123,44,7};
    static_assert(user.control.valid());
    static_assert(npc.control.valid());
    static_assert(user.control.can_issue_player_order(7));
    static_assert(!user.control.can_issue_player_order(8));
    static_assert(!npc.control.can_issue_player_order(7));
    static_assert(npc.control.can_run_delegated_npc_order(44,true));
    static_assert(!npc.control.can_run_delegated_npc_order(44,false));
    static_assert(!npc.control.can_run_delegated_npc_order(45,true));
    static_assert(!user.control.can_run_delegated_npc_order(7,true));
    // Runtime checks must remain active in Release (-DNDEBUG) builds.
    // Macro assert cannot parse brace-initializer commas as one argument.
    if(!UserShip{99,9}.control.can_issue_player_order(9))return 1;
    if(NpcShip{99,19,9}.control.can_issue_player_order(9))return 1;
}
