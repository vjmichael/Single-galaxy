#!/usr/bin/env python3
"""FLIGHT-03 exact-anchor source integration. Fail closed on any upstream drift."""
from pathlib import Path
p=Path("src/Simulation.cpp")
s=p.read_text(encoding="utf-8")
def change(old,new):
    global s
    count=s.count(old)
    if count!=1:
        raise RuntimeError(f"FLIGHT-03 expected one anchor, got {count}: {old[:96]!r}")
    s=s.replace(old,new,1)

# Reuse the same admission, crew reservation and native flight ledger.
start=s.index("bool Simulation::queue_npc_station_docking(")
end=s.index("// One original command per committed",start)
source=s[start:end]
assert source.count("bool Simulation::queue_npc_station_docking(")==1
r=source.replace("queue_npc_station_docking(","queue_npc_ship_rendezvous(")
r=r.replace("station=entities_.find(target)","destination=entities_.find(target)")
r=r.replace("station==entities_.end()","destination==entities_.end()")
r=r.replace("station->second.type!=EntityType::Station","destination->second.type!=EntityType::Ship")
r=r.replace("station->second.system_id","destination->second.system_id")
r=r.replace("       !same_local_frame(ship,target) ||","       ship==target || !same_local_frame(ship,target) ||")
r=r.replace("""       (vessel->second.dock_state==DockState::Docked &&
        vessel->second.dock_station==target) ||""","""       destination->second.dock_state!=DockState::InSpace ||
       destination->second.jump_state!=ShipJumpState::Ready ||""")
r=r.replace("order.mode=NpcFlightMode::LocalDock;","order.mode=NpcFlightMode::LocalRendezvous;")
r=r.replace("std::max(snapshot_version_,96)","std::max(snapshot_version_,97)")
assert "station->" not in r and "station==" not in r
change(source,source+r)

change("if(order.mode!=NpcFlightMode::LocalDock ||",
       "if((order.mode!=NpcFlightMode::LocalDock &&\n        order.mode!=NpcFlightMode::LocalRendezvous) ||")
change("if(order.mode==NpcFlightMode::LocalDock){",
       "if(order.mode==NpcFlightMode::LocalDock ||\n           order.mode==NpcFlightMode::LocalRendezvous){")
change("""const auto station=entities_.find(order.station);
            if(ship==entities_.end() || station==entities_.end() ||
               ship->second.type!=EntityType::Ship ||
               station->second.type!=EntityType::Station ||
               ship->second.owner_id!=order.owner ||
               ship->second.system_id!=station->second.system_id ||
               !same_local_frame(order.ship,order.station))continue;""",
       """const auto station=entities_.find(order.station);
            const bool rendezvous=order.mode==NpcFlightMode::LocalRendezvous;
            if(ship==entities_.end() || station==entities_.end() ||
               ship->second.type!=EntityType::Ship ||
               station->second.type!=(rendezvous?EntityType::Ship:EntityType::Station) ||
               ship->second.owner_id!=order.owner ||
               ship->second.system_id!=station->second.system_id ||
               order.ship==order.station ||
               !same_local_frame(order.ship,order.station)){
                if(rendezvous){
                    order.phase=NpcFlightPhase::Cancelled;
                    order.resolved_tick=tick_;
                }
                continue;
            }
            if(rendezvous && (station->second.dock_state!=DockState::InSpace ||
                              station->second.jump_state!=ShipJumpState::Ready)){
                order.phase=NpcFlightPhase::Cancelled;
                order.resolved_tick=tick_;
                continue;
            }""")
change("""if(ship->second.dock_state==DockState::Docked &&
               ship->second.dock_station==order.station){""",
       """if(!rendezvous && ship->second.dock_state==DockState::Docked &&
               ship->second.dock_station==order.station){""")
change("""const auto nav=assist_to_object(actual,station->second,1500);
            if(nav.arrived)
                (void)submit_npc_local_control(
                    order,Action::RequestDock,order.station);
            else
                (void)submit_npc_local_control(
                    order,Action::Thrust,0,nav.thrust_x,nav.thrust_y);""",
       """const auto nav=assist_to_object(actual,station->second,1500);
            if(nav.arrived){
                if(rendezvous){
                    bool applied=false;
                    for(const auto& ev:events_)
                        if(ev.command_id==order.last_local_command &&
                           ev.result==Outcome::Applied)applied=true;
                    if(applied && order.phase==NpcFlightPhase::InTransit){
                        order.phase=NpcFlightPhase::Arrived;
                        order.resolved_tick=tick_;
                        activity_events_.push_back({tick_,
                            ActivityKind::NpcRendezvous,order.last_local_command,
                            order.ship,order.station,Outcome::Applied,0});
                    }
                }else
                    (void)submit_npc_local_control(
                        order,Action::RequestDock,order.station);
            }else
                (void)submit_npc_local_control(
                    order,Action::Thrust,0,nav.thrust_x,nav.thrust_y);""")

# V97 preserves the identical v96 flight-row wire shape.
change('snapshot_version_ >= 96 ? "galaxy-kernel-v96|"',
       'snapshot_version_ >= 97 ? "galaxy-kernel-v97|" : snapshot_version_ >= 96 ? "galaxy-kernel-v96|"')
change('h[0] != "galaxy-kernel-v96")',
       'h[0] != "galaxy-kernel-v96" && h[0] != "galaxy-kernel-v97")')
change('result.snapshot_version_ = h[0] == "galaxy-kernel-v96" ? 96',
       'result.snapshot_version_ = h[0] == "galaxy-kernel-v97" ? 97 : h[0] == "galaxy-kernel-v96" ? 96')
change("mode>unsigned(NpcFlightMode::LocalDock) ||",
       "mode>(result.snapshot_version_>=97?unsigned(NpcFlightMode::LocalRendezvous):unsigned(NpcFlightMode::LocalDock)) ||")
change("(mode==1 && (id>=(std::uint64_t{1}<<40) ||",
       "((mode==1 || mode==2) && (id>=(std::uint64_t{1}<<40) ||")
change("""const auto target=result.entities_.find(flight.station);
                if(result.snapshot_version_<96 ||
                   target==result.entities_.end() ||
                   target->second.type!=EntityType::Station ||
                   target->second.system_id!=ship->second.system_id ||
                   !result.same_local_frame(flight.ship,flight.station))""",
       """const auto target=result.entities_.find(flight.station);
                const bool rendezvous=flight.mode==NpcFlightMode::LocalRendezvous;
                if(result.snapshot_version_<96 ||
                   (rendezvous && result.snapshot_version_<97) ||
                   target==result.entities_.end() ||
                   target->second.type!=(rendezvous?EntityType::Ship:EntityType::Station) ||
                   flight.station==flight.ship ||
                   target->second.system_id!=ship->second.system_id ||
                   !result.same_local_frame(flight.ship,flight.station))""")
change("""if(flight.phase==NpcFlightPhase::Arrived){
                    const bool docked=ship->second.dock_state==DockState::Docked &&
                        ship->second.dock_station==flight.station;
                    bool applied=false,dock_receipt=false;""",
       """if(flight.phase==NpcFlightPhase::Arrived){
                    const bool docked=ship->second.dock_state==DockState::Docked &&
                        ship->second.dock_station==flight.station;
                    bool applied=false,dock_receipt=false;""")
change("""if(event.kind==ActivityKind::Docked &&
                           event.actor==flight.ship &&""",
       """if(event.kind==(rendezvous?ActivityKind::NpcRendezvous:ActivityKind::Docked) &&
                           (!rendezvous || (event.start_command_id==flight.last_local_command &&
                                            event.tick==flight.resolved_tick &&
                                            event.result==Outcome::Applied)) &&
                           event.actor==flight.ship &&""")
change("""if(!docked || !applied || !dock_receipt)
                        throw std::runtime_error("snapshot: false NPC station arrival");""",
       """if(!applied || !dock_receipt || (!rendezvous && !docked))
                        throw std::runtime_error("snapshot: false NPC local arrival");""")
change("""if(flight.phase==NpcFlightPhase::InTransit &&
                   ship->second.dock_state==DockState::Docked &&""",
       """if(!rendezvous && flight.phase==NpcFlightPhase::InTransit &&
                   ship->second.dock_state==DockState::Docked &&""")
change("result.snapshot_version_>=27 ? static_cast<unsigned>(ActivityKind::AutoStorageTransfer)",
       "result.snapshot_version_>=97 ? static_cast<unsigned>(ActivityKind::NpcRendezvous) : result.snapshot_version_>=27 ? static_cast<unsigned>(ActivityKind::AutoStorageTransfer)")

p.write_text(s,encoding="utf-8")
print("FLIGHT-03: exact-anchor native integration applied.")
