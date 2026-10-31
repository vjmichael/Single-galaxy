#include "galaxy/Persistence.hpp"
#include "galaxy/ClientContract.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace galaxy;
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #x); } while(false)

struct TempSave {
    fs::path folder, file;
    TempSave() {
        static int counter=0;
        folder = fs::temp_directory_path() / ("galaxy_station_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 "_" + std::to_string(++counter));
        fs::create_directories(folder); file=folder/"station.sqlite";
    }
    ~TempSave() { std::error_code ec; fs::remove_all(folder,ec); }
};

Event act(PersistentWorld& p, std::uint64_t id, PlayerId player, EntityId ship,
          EntityId station_or_rock, Action action, std::int64_t amount=0,
          int thrust_x=0, FailPoint fail=FailPoint::None) {
    Command c;
    c.id=id; c.execute_tick=p.world().tick()+1; c.issuer=player; c.actor=ship;
    c.target=station_or_rock; c.action=action; c.amount=amount; c.thrust_x=thrust_x;
    REQUIRE(p.submit(c));
    p.step(fail);
    auto out=p.world().events().back();
    REQUIRE(out.command_id==id);
    return out;
}
void step(PersistentWorld& p, std::uint64_t n=1) { p.steps(n); }
void must_fail(const std::function<void()>& f) {
    bool failed=false; try {f();} catch (const std::exception&) {failed=true;}
    REQUIRE(failed);
}
void leave_station(PersistentWorld& p, EntityId ship, EntityId station, std::uint64_t& id) {
    REQUIRE(p.world().find(ship)->dock_state==DockState::Docked);
    REQUIRE(act(p,++id,7,ship,station,Action::RequestUndock).result==Outcome::Applied);
    REQUIRE(p.world().find(ship)->dock_state==DockState::Undocking);
    REQUIRE(p.world().find(ship)->position.x==0);
    step(p); REQUIRE(p.world().find(ship)->dock_state==DockState::Undocking);
    step(p); REQUIRE(p.world().find(ship)->dock_state==DockState::InSpace);
    REQUIRE(p.world().find(ship)->position.x==Simulation::kUndockOffsetMm);
    REQUIRE(act(p,++id,7,ship,0,Action::Thrust,0,1).result==Outcome::Applied);
    step(p); REQUIRE(p.world().find(ship)->position.x==2000);
    REQUIRE(act(p,++id,7,ship,0,Action::Thrust,0,-1).result==Outcome::Applied);
    REQUIRE(p.world().find(ship)->velocity.x==0);
}
void dock_again(PersistentWorld& p, EntityId ship, EntityId station, std::uint64_t& id) {
    REQUIRE(act(p,++id,7,ship,0,Action::Thrust,0,-1).result==Outcome::Applied);
    REQUIRE(p.world().find(ship)->velocity.x==-Simulation::kAccelerationMmPerTick2);
    REQUIRE(act(p,++id,7,ship,0,Action::Thrust,0,1).result==Outcome::Applied);
    REQUIRE(p.world().find(ship)->velocity.x==0);
    REQUIRE(p.world().find(ship)->position.x==1750);
    REQUIRE(act(p,++id,7,ship,station,Action::RequestDock).result==Outcome::Applied);
    REQUIRE(p.world().find(ship)->dock_state==DockState::Docking);
    step(p); REQUIRE(p.world().find(ship)->dock_state==DockState::Docking);
    step(p); REQUIRE(p.world().find(ship)->dock_state==DockState::Docked);
    REQUIRE(p.world().find(ship)->position.x==0);
}
void full_unload_round_trip() {
    TempSave db;
    Simulation setup(5001);
    const auto station=setup.add_station({0,0});
    const auto far_station=setup.add_station({30000,0});
    const auto ship=setup.spawn_ship_docked(7,station,20,25);
    const auto asteroid=setup.add_asteroid({7000,0},15);
    std::uint64_t id=1000;
    {
        PersistentWorld p(db.file.string(),setup);
        REQUIRE(p.world().find(ship)->dock_state==DockState::Docked);
        REQUIRE(p.world().hangar(station,7)->ore==0);
        REQUIRE(act(p,++id,7,ship,asteroid,Action::StartMining,5).result==Outcome::NotInSpace);
        leave_station(p,ship,station,id);
        REQUIRE(act(p,++id,7,ship,asteroid,Action::StartMining,5).result==Outcome::Applied);
        step(p,2);
        REQUIRE(p.world().find(ship)->cargo==15);
        REQUIRE(p.world().was_depleted(asteroid));
        REQUIRE(p.world().total_ore()==15);
        REQUIRE(p.world().active_mining().empty());
        REQUIRE(act(p,++id,7,ship,far_station,Action::RequestDock).result==Outcome::OutOfRange);
        dock_again(p,ship,station,id);
        REQUIRE(act(p,++id,7,ship,far_station,Action::UnloadOre,100).result==Outcome::InvalidTarget);
        const auto receipt=act(p,++id,7,ship,station,Action::UnloadOre,100);
        REQUIRE(receipt.result==Outcome::Applied && receipt.transferred==15);
        REQUIRE(p.world().find(ship)->cargo==0);
        REQUIRE(p.world().hangar(station,7)->ore==15);
        REQUIRE(p.world().hangar(far_station,7)==nullptr);
        REQUIRE(p.world().total_ore()==15);
        std::cout << "SCENARIO A: spawn docked -> undock (2 ticks) -> fly -> mine 15 -> return/dock (2 ticks) -> unload 15/15; ship=0, local hangar=15, remote hangar=absent, ore conserved=15\n";
    }
    PersistentWorld reopen(db.file.string(),Simulation(123));
    REQUIRE(reopen.world().find(ship)->dock_state==DockState::Docked);
    REQUIRE(reopen.world().hangar(station,7)->ore==15);
    REQUIRE(reopen.world().find(ship)->cargo==0);
    REQUIRE(reopen.world().hangar(far_station,7)==nullptr);
    REQUIRE(reopen.world().total_ore()==15);
}
void partial_unload_round_trip() {
    TempSave db;
    Simulation setup(5002);
    const auto station=setup.add_station({0,0});
    const auto remote=setup.add_station({30000,0});
    const auto ship=setup.spawn_ship_docked(7,station,20,7);
    const auto asteroid=setup.add_asteroid({7000,0},20);
    std::uint64_t id=2000;
    {
        PersistentWorld p(db.file.string(),setup);
        leave_station(p,ship,station,id);
        REQUIRE(act(p,++id,7,ship,asteroid,Action::StartMining,10).result==Outcome::Applied);
        step(p); // second mining cycle
        REQUIRE(p.world().find(ship)->cargo==20);
        REQUIRE(p.world().was_depleted(asteroid));
        REQUIRE(p.world().total_ore()==20);
        dock_again(p,ship,station,id);
        auto ev=act(p,++id,7,ship,station,Action::UnloadOre,100);
        REQUIRE(ev.result==Outcome::Applied && ev.transferred==7);
        REQUIRE(p.world().find(ship)->cargo==13);
        REQUIRE(p.world().hangar(station,7)->ore==7);
        REQUIRE(p.world().total_ore()==20);
        ev=act(p,++id,7,ship,station,Action::UnloadOre,100);
        REQUIRE(ev.result==Outcome::HangarFull && ev.transferred==0);
        REQUIRE(p.world().hangar(remote,7)==nullptr);
        std::cout << "SCENARIO B: ship hold=20, local hangar=7, mined=20; unload requested=100, transferred=7, ship retains=13, local hangar=7/7, ore conserved=20; further unload HANGAR_FULL\n";
    }
    PersistentWorld reopened(db.file.string(),Simulation(333));
    REQUIRE(reopened.world().find(ship)->cargo==13);
    REQUIRE(reopened.world().hangar(station,7)->ore==7);
    REQUIRE(reopened.world().hangar(remote,7)==nullptr);
    REQUIRE(reopened.world().total_ore()==20);
}
void authority_and_transition_restrictions() {
    TempSave db;
    Simulation init(7);
    auto station=init.add_station({0,0});
    auto player_ship=init.spawn_ship_docked(7,station,20,50);
    auto other_ship=init.spawn_ship_docked(8,station,20,50);
    auto asteroid=init.add_asteroid({2000,0},20);
    PersistentWorld p(db.file.string(),init);
    std::uint64_t id=3000;
    REQUIRE(act(p,++id,8,player_ship,station,Action::RequestUndock).result==Outcome::Unauthorized);
    REQUIRE(act(p,++id,7,player_ship,station,Action::RequestUndock).result==Outcome::Applied);
    REQUIRE(act(p,++id,7,player_ship,0,Action::Thrust,0,1).result==Outcome::NotInSpace);
    REQUIRE(p.world().find(player_ship)->dock_state==DockState::Undocking);
    step(p); // complete undock at the next committed tick
    REQUIRE(p.world().find(player_ship)->dock_state==DockState::InSpace);
    REQUIRE(act(p,++id,7,player_ship,station,Action::RequestUndock).result==Outcome::NotDocked);
    REQUIRE(act(p,++id,7,player_ship,station,Action::UnloadOre,10).result==Outcome::NotDocked);
    REQUIRE(act(p,++id,7,player_ship,station,Action::RequestDock).result==Outcome::Applied);
    REQUIRE(act(p,++id,7,player_ship,station,Action::UnloadOre,10).result==Outcome::InTransition);
    step(p); // unload rejection already advanced one docking tick
    REQUIRE(p.world().find(player_ship)->dock_state==DockState::Docked);
    REQUIRE(p.world().find(other_ship)->dock_state==DockState::Docked);
    REQUIRE(p.world().hangar(station,8)->ore==0);
    REQUIRE(p.world().find(asteroid)->ore==20);
}
void moving_ship_cannot_dock_and_remote_hangar_is_local() {
    TempSave db;
    Simulation init(8);
    auto home=init.add_station({0,0});
    auto remote=init.add_station({1500,0});
    auto ship=init.spawn_ship_docked(7,home,20,5);
    auto rock=init.add_asteroid({2300,0},10);
    PersistentWorld p(db.file.string(),init);
    std::uint64_t id=4000;
    REQUIRE(act(p,++id,7,ship,home,Action::RequestUndock).result==Outcome::Applied);
    step(p,2);
    REQUIRE(act(p,++id,7,ship,0,Action::Thrust,0,1).result==Outcome::Applied);
    REQUIRE(act(p,++id,7,ship,remote,Action::RequestDock).result==Outcome::ShipMoving);
    REQUIRE(act(p,++id,7,ship,0,Action::Thrust,0,-1).result==Outcome::Applied);
    REQUIRE(act(p,++id,7,ship,rock,Action::Mine,10).result==Outcome::Applied);
    REQUIRE(p.world().find(ship)->cargo==10);
    REQUIRE(act(p,++id,7,ship,remote,Action::RequestDock).result==Outcome::Applied);
    step(p,2);
    REQUIRE(p.world().find(ship)->dock_state==DockState::Docked);
    REQUIRE(p.world().hangar(remote,7) && p.world().hangar(remote,7)->ore==0);
    REQUIRE(p.world().hangar(home,7)->ore==0);
    REQUIRE(act(p,++id,7,ship,remote,Action::UnloadOre,6).transferred==6);
    REQUIRE(p.world().hangar(remote,7)->ore==6);
    REQUIRE(p.world().hangar(home,7)->ore==0);
    REQUIRE(p.world().find(ship)->cargo==4);
    REQUIRE(p.world().total_ore()==10);
    PersistentWorld reload(db.file.string(),Simulation(999));
    REQUIRE(reload.world().hangar(remote,7)->ore==6);
    REQUIRE(reload.world().hangar(home,7)->ore==0);
}
void failure_rolls_back_hangar_transfer() {
    TempSave db;
    Simulation init(9);
    auto station=init.add_station({0,0});
    auto ship=init.spawn_ship_docked(7,station,20,7);
    auto rock=init.add_asteroid({2000,0},10);
    {
        PersistentWorld p(db.file.string(),init);
        std::uint64_t id=5000;
        REQUIRE(act(p,++id,7,ship,station,Action::RequestUndock).result==Outcome::Applied);
        step(p,2);
        REQUIRE(act(p,++id,7,ship,rock,Action::Mine,10).transferred==10);
        REQUIRE(act(p,++id,7,ship,station,Action::RequestDock).result==Outcome::Applied);
        step(p,2);
        Command unload; unload.id=++id; unload.execute_tick=p.world().tick()+1;
        unload.issuer=7; unload.actor=ship; unload.target=station;
        unload.action=Action::UnloadOre; unload.amount=100;
        REQUIRE(p.submit(unload));
        const auto old_hash=p.world().state_hash();
        must_fail([&]{p.step(FailPoint::AfterSnapshotWrite);});
        REQUIRE(p.world().state_hash()==old_hash);
        REQUIRE(p.world().find(ship)->cargo==10);
        REQUIRE(p.world().hangar(station,7)->ore==0);
        p.step();
        REQUIRE(p.world().events().back().transferred==7);
        REQUIRE(p.world().find(ship)->cargo==3 && p.world().hangar(station,7)->ore==7);
        REQUIRE(p.world().total_ore()==10);
    }
    PersistentWorld p(db.file.string(),Simulation(987));
    REQUIRE(p.world().find(ship)->cargo==3);
    REQUIRE(p.world().hangar(station,7)->ore==7);
    REQUIRE(p.world().total_ore()==10);
}
void snapshot_validation_rejects_bogus_station() {
    Simulation s(4);
    auto station=s.add_station({0,0});
    auto ship=s.spawn_ship_docked(7,station,10,10);
    const auto original=s.canonical_state();
    REQUIRE(Simulation::from_snapshot(original).canonical_state()==original);
    std::string tampered=original;
    const std::string needle="hangar|"+std::to_string(station)+"|7|0|10";
    auto at=tampered.find(needle);
    REQUIRE(at!=std::string::npos);
    tampered.replace(at,needle.size(),"hangar|999999|7|0|10");
    must_fail([&]{(void)Simulation::from_snapshot(tampered);});
    auto ship_copy=s.find(ship); REQUIRE(ship_copy && ship_copy->dock_state==DockState::Docked);
}

void restart_during_station_transitions() {
    TempSave db;
    Simulation init(15);
    auto station=init.add_station({0,0});
    auto ship=init.spawn_ship_docked(7,station,20,8);
    {
        PersistentWorld p(db.file.string(),init);
        REQUIRE(act(p,6001,7,ship,station,Action::RequestUndock).result==Outcome::Applied);
        REQUIRE(p.world().find(ship)->transition_ticks==2);
    }
    {
        PersistentWorld p(db.file.string(),Simulation(1));
        REQUIRE(p.world().find(ship)->dock_state==DockState::Undocking);
        step(p);
        REQUIRE(p.world().find(ship)->transition_ticks==1);
    }
    {
        PersistentWorld p(db.file.string(),Simulation(1));
        step(p);
        REQUIRE(p.world().find(ship)->dock_state==DockState::InSpace);
        REQUIRE(act(p,6002,7,ship,station,Action::RequestDock).result==Outcome::Applied);
        REQUIRE(p.world().find(ship)->transition_ticks==2);
        step(p);
        REQUIRE(p.world().find(ship)->transition_ticks==1);
    }
    PersistentWorld final(db.file.string(),Simulation(1));
    REQUIRE(final.world().find(ship)->dock_state==DockState::Docking);
    step(final);
    REQUIRE(final.world().find(ship)->dock_state==DockState::Docked);
    REQUIRE(final.world().hangar(station,7)->ore==0);
}
void recovered_copy_replays_partial_unload() {
    TempSave db;
    Simulation init(16);
    auto station=init.add_station({0,0});
    auto ship=init.spawn_ship_docked(7,station,20,7);
    auto rock=init.add_asteroid({2000,0},10);
    {
        PersistentWorld p(db.file.string(),init);
        std::uint64_t id=7000;
        REQUIRE(act(p,++id,7,ship,station,Action::RequestUndock).result==Outcome::Applied);
        step(p,2);
        REQUIRE(act(p,++id,7,ship,rock,Action::Mine,10).transferred==10);
        REQUIRE(act(p,++id,7,ship,station,Action::RequestDock).result==Outcome::Applied);
        step(p,2);
        REQUIRE(act(p,++id,7,ship,station,Action::UnloadOre,100).transferred==7);
        REQUIRE(p.world().find(ship)->cargo==3);
    }
    sqlite3* connection=nullptr;
    REQUIRE(sqlite3_open(db.file.string().c_str(),&connection)==SQLITE_OK);
    char* error=nullptr;
    const auto rc=sqlite3_exec(connection,
        "UPDATE snapshots SET state=x'00424144' WHERE generation=(SELECT MAX(generation) FROM snapshots)",
        nullptr,nullptr,&error);
    sqlite3_free(error);
    sqlite3_close(connection);
    REQUIRE(rc==SQLITE_OK);
    must_fail([&]{PersistentWorld corrupted(db.file.string(),Simulation(1));});
    const auto repaired=(db.folder/"verified_recovery.sqlite").string();
    PersistentWorld::recover_copy(db.file.string(),repaired);
    PersistentWorld clean(repaired,Simulation(1));
    REQUIRE(clean.world().find(ship)->dock_state==DockState::Docked);
    REQUIRE(clean.world().find(ship)->cargo==3);
    REQUIRE(clean.world().hangar(station,7)->ore==7);
    REQUIRE(clean.world().total_ore()==10);
    must_fail([&]{PersistentWorld still_corrupted(db.file.string(),Simulation(1));});
}

void station_events_cross_client_contract() {
    TempSave db;
    Simulation init(17);
    auto station=init.add_station({0,0});
    auto ship=init.spawn_ship_docked(7,station,20,7);
    auto rock=init.add_asteroid({2000,0},10);
    PersistentWorld p(db.file.string(),init);
    ServerViewBridge bridge(p.world(),1700);
    ClientView viewer;
    REQUIRE(viewer.apply(decode_frame(encode_frame(bridge.snapshot())))==ApplyResult::Applied);
    auto advance=[&](std::uint64_t id,Action action,EntityId target,std::int64_t amount=0) {
        if (id) {
            Command command;command.id=id;command.execute_tick=p.world().tick()+1;
            command.issuer=7;command.actor=ship;command.target=target;
            command.action=action;command.amount=amount;
            REQUIRE(p.submit(command));
        }
        p.step();
        REQUIRE(viewer.apply(decode_frame(encode_frame(bridge.after_committed_step(p.world()))))==ApplyResult::Applied);
    };
    advance(8001,Action::RequestUndock,station);
    advance(0,Action::Thrust,0);
    advance(0,Action::Thrust,0);
    bool saw_undocked=false;
    for (const auto& ev: viewer.recent_notifications())
        if (ev.kind==ChangeKind::Activity && ev.activity.kind==ActivityKind::Undocked) saw_undocked=true;
    REQUIRE(saw_undocked);
    advance(8002,Action::Mine,rock,10);
    REQUIRE(viewer.world().entities.at(ship).cargo==10);
    REQUIRE(!viewer.world().entities.count(rock));
    advance(8003,Action::RequestDock,station);
    advance(0,Action::Thrust,0);
    advance(0,Action::Thrust,0);
    bool saw_docked=false;
    for (const auto& ev: viewer.recent_notifications())
        if (ev.kind==ChangeKind::Activity && ev.activity.kind==ActivityKind::Docked) saw_docked=true;
    REQUIRE(saw_docked);
    advance(8004,Action::UnloadOre,station,100);
    REQUIRE(viewer.world().entities.at(ship).cargo==3);
    bool saw_unload=false;
    for (const auto& ev: viewer.recent_notifications())
        if (ev.kind==ChangeKind::CommandResult && ev.command.command_id==8004 &&
            ev.command.result==Outcome::Applied && ev.command.transferred==7) saw_unload=true;
    REQUIRE(saw_unload);
    REQUIRE(p.world().hangar(station,7)->ore==7);
}
void tampered_hangar_ledger_is_rejected() {
    TempSave db;
    Simulation init(18);
    auto station=init.add_station({0,0});
    auto ship=init.spawn_ship_docked(7,station,20,7);
    auto rock=init.add_asteroid({2000,0},10);
    {
        PersistentWorld p(db.file.string(),init);
        REQUIRE(act(p,9001,7,ship,station,Action::RequestUndock).result==Outcome::Applied);
        step(p,2);
        REQUIRE(act(p,9002,7,ship,rock,Action::Mine,10).transferred==10);
        REQUIRE(act(p,9003,7,ship,station,Action::RequestDock).result==Outcome::Applied);
        step(p,2);
        REQUIRE(act(p,9004,7,ship,station,Action::UnloadOre,100).transferred==7);
    }
    sqlite3* connection=nullptr;
    REQUIRE(sqlite3_open(db.file.string().c_str(),&connection)==SQLITE_OK);
    char* error=nullptr;
    const auto rc=sqlite3_exec(connection,
        "UPDATE resource_ledger SET destination_id='hangar:999:owner:7' WHERE reason='hangar_transfer'",
        nullptr,nullptr,&error);
    sqlite3_free(error);
    sqlite3_close(connection);
    REQUIRE(rc==SQLITE_OK);
    must_fail([&]{PersistentWorld corrupted(db.file.string(),Simulation(1));});
}

int main() {
    struct Test { const char* name; void(*fn)(); } tests[]={
        {"full_unload_round_trip",full_unload_round_trip},
        {"partial_unload_round_trip",partial_unload_round_trip},
        {"authority_and_transition_restrictions",authority_and_transition_restrictions},
        {"moving_ship_cannot_dock_and_remote_hangar_is_local",moving_ship_cannot_dock_and_remote_hangar_is_local},
        {"failure_rolls_back_hangar_transfer",failure_rolls_back_hangar_transfer},
        {"snapshot_validation_rejects_bogus_station",snapshot_validation_rejects_bogus_station},
        {"restart_during_station_transitions",restart_during_station_transitions},
        {"recovered_copy_replays_partial_unload",recovered_copy_replays_partial_unload},
        {"station_events_cross_client_contract",station_events_cross_client_contract},
        {"tampered_hangar_ledger_is_rejected",tampered_hangar_ledger_is_rejected},
    };
    std::size_t passed=0;
    for(const auto& test:tests) {
        try {test.fn(); ++passed; std::cout<<"PASS "<<test.name<<'\n';}
        catch(const std::exception& error) {std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<'\n'; return 1;}
    }
    std::cout<<"PASS "<<passed<<"/"<<std::size(tests)<<" station lifecycle tests\n";
}
