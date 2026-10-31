#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" check failed: " #x); } while(false)
struct TempDb {
    fs::path folder, file;
    TempDb() {
        static int counter=0;
        folder=fs::temp_directory_path()/("galaxy_b3_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"_"+std::to_string(++counter));
        fs::create_directories(folder); file=folder/"world.sqlite";
    }
    ~TempDb(){std::error_code error;fs::remove_all(folder,error);}
};
Command mining(std::uint64_t id, std::uint64_t tick, EntityId ship, EntityId rock, Action action, std::int64_t amount){
    Command c; c.id=id; c.execute_tick=tick; c.issuer=7; c.actor=ship;
    c.target=rock; c.action=action; c.amount=amount; return c;
}
ViewFrame round_trip(const ViewFrame& f){return decode_frame(encode_frame(f));}
void expect_throw(const std::function<void()>& fn){bool yes=false;try{fn();}catch(const std::exception&){yes=true;} CHECK(yes);}
std::size_t count_activity(const ClientView& view, ActivityKind kind, Outcome outcome) {
    std::size_t count=0;
    for(const auto& c : view.recent_notifications())
        if(c.kind==ChangeKind::Activity && c.activity.kind==kind && c.activity.result==outcome) ++count;
    return count;
}
void start_snapshot() {
    Simulation s(7); s.add_ship(7,{0,0},100);s.add_asteroid({1000,0},50);s.add_station({5000,0});
    ServerViewBridge srv(s,9001); ClientView client;
    const auto frame=round_trip(srv.snapshot());
    CHECK(frame.sequence==0 && frame.tick==0 && frame.changes.size()==3);
    CHECK(client.apply(frame)==ApplyResult::Applied);
    CHECK(client.world().entities.size()==3 && client.recent_notifications().empty());
    CHECK(projection_hash(client.world())==frame.projection_hash);
}
void ship_movement_delta() {
    Simulation s(1); auto ship=s.add_ship(7,{0,0},50);
    ServerViewBridge srv(s,9002);ClientView client;CHECK(client.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    Command c; c.id=1;c.execute_tick=1;c.issuer=7;c.actor=ship;c.action=Action::Thrust;c.thrust_x=1;
    CHECK(s.submit(c));s.step(); auto d=round_trip(srv.after_committed_step(s));
    CHECK(client.apply(d)==ApplyResult::Applied);
    CHECK(client.world().entities.at(ship).position.x==250);
    CHECK(client.world().entities.at(ship).velocity.x==250);
    CHECK(client.recent_notifications().size()==1);
    CHECK(client.recent_notifications()[0].command.result==Outcome::Applied);
}
void ten_asteroids_remove_and_persist() {
    TempDb db;Simulation seed(12);auto ship=seed.add_ship(7,{0,0},100);
    std::vector<EntityId> rocks;
    for(int i=0;i<10;++i)rocks.push_back(seed.add_asteroid({200+i*100,0},10));
    {
        PersistentWorld w(db.file.string(),seed);ServerViewBridge srv(w.world(),3001);ClientView client;
        CHECK(client.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
        for(int i=0;i<10;++i){
            CHECK(w.submit(mining(100+i,i+1,ship,rocks[i],Action::Mine,10)));
            w.step(); auto d=round_trip(srv.after_committed_step(w.world()));
            CHECK(client.apply(d)==ApplyResult::Applied);
            CHECK(!client.world().entities.count(rocks[i]));
            CHECK(client.world().entities.at(ship).cargo==10*(i+1));
            CHECK(count_activity(client,ActivityKind::AsteroidDespawned,Outcome::NoResources)==1);
        }
        CHECK(client.world().entities.size()==1);
        CHECK(count_activity(client,ActivityKind::MiningStopped,Outcome::CargoFull)==1);
        std::cout << "10 asteroids: 10 removals observed, cargo 100/100, no phantom rocks\n";
    }
    PersistentWorld reopened(db.file.string(), Simulation(99));
    ServerViewBridge restarted(reopened.world(),3002);ClientView again;
    CHECK(again.apply(round_trip(restarted.snapshot()))==ApplyResult::Applied);
    CHECK(again.world().entities.size()==1 && again.world().entities.at(ship).cargo==100);
    CHECK(again.recent_notifications().empty());
    CHECK(reopened.world().total_ore()==100);
}
void automatic_full_stop_notifications() {
    TempDb db;Simulation seed(2);auto ship=seed.add_ship(7,{0,0},10),rock=seed.add_asteroid({500,0},20);
    PersistentWorld w(db.file.string(),seed);ServerViewBridge srv(w.world(),4001);ClientView client;
    CHECK(client.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    CHECK(w.submit(mining(1,1,ship,rock,Action::StartMining,4)));
    for(int t=1;t<=3;++t){
        w.step();CHECK(client.apply(round_trip(srv.after_committed_step(w.world())))==ApplyResult::Applied);
        CHECK(client.world().entities.at(ship).cargo==(t==3?10:4*t));
        CHECK(count_activity(client,ActivityKind::AutoMiningTransfer,Outcome::Applied)==1);
    }
    CHECK(client.world().entities.at(rock).ore==10);
    CHECK(client.world().mining.empty());
    CHECK(count_activity(client,ActivityKind::MiningStopped,Outcome::CargoFull)==1);
    for(int t=0;t<4;++t){
        w.step();CHECK(client.apply(round_trip(srv.after_committed_step(w.world())))==ApplyResult::Applied);
        CHECK(client.recent_notifications().empty());
        CHECK(client.world().entities.at(ship).cargo==10);
    }
    std::cout << "20 ore / 10 cargo: 4+4+2; CARGO_FULL delivered once; no extra transfers\n";
}
void dropped_packet_and_snapshot_reconnect() {
    Simulation s(1);auto ship=s.add_ship(7,{0,0},20);ServerViewBridge srv(s,5001);ClientView c;
    CHECK(c.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    auto tick=[&]{s.step();return round_trip(srv.after_committed_step(s));};
    auto first=tick();auto missed=tick();auto third=tick();(void)missed;
    CHECK(c.apply(first)==ApplyResult::Applied);
    const auto before=projection_hash(c.world());
    CHECK(c.apply(third)==ApplyResult::NeedsSnapshot);
    CHECK(projection_hash(c.world())==before);
    CHECK(c.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    CHECK(c.world().tick==3 && c.world().entities.at(ship).id==ship);
    CHECK(c.recent_notifications().empty());
}
void duplicate_and_old_stream_rejected() {
    Simulation s(1);s.add_ship(7,{0,0},10);ServerViewBridge one(s,7001);ClientView c;
    CHECK(c.apply(round_trip(one.snapshot()))==ApplyResult::Applied);
    s.step();auto d=round_trip(one.after_committed_step(s));
    CHECK(c.apply(d)==ApplyResult::Applied);
    CHECK(c.apply(d)==ApplyResult::DuplicateIgnored);
    ServerViewBridge newer(s,7002);auto next=round_trip(newer.snapshot());
    CHECK(c.apply(next)==ApplyResult::Applied);
    CHECK(c.apply(d)==ApplyResult::NeedsSnapshot);
    CHECK(c.stream_id()==7002 && c.world().tick==1);
}
void forged_hash_and_invalid_frame_atomicity() {
    Simulation s(1);auto ship=s.add_ship(7,{0,0},10);ServerViewBridge srv(s,100);ClientView c;
    CHECK(c.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    const auto old_hash=projection_hash(c.world());
    s.step();auto d=round_trip(srv.after_committed_step(s));
    d.projection_hash ^= 1;
    expect_throw([&]{c.apply(d);});
    CHECK(projection_hash(c.world())==old_hash && c.world().tick==0);
    d.projection_hash ^= 1;
    CHECK(c.apply(d)==ApplyResult::Applied);
    (void)ship;
}
void malformed_wire_rejected() {
    Simulation s(1);s.add_ship(7,{0,0},10);ServerViewBridge srv(s,6001);
    std::string good=encode_frame(srv.snapshot());
    expect_throw([&]{decode_frame(good.substr(0,good.size()-3));});
    expect_throw([&]{decode_frame("BAD"+good);});
    expect_throw([&]{decode_frame(good+"E|2\n");});
    expect_throw([&]{decode_frame(good.substr(0,good.size()-1));});
    std::string noncanonical=good;
    auto pos=noncanonical.find("SGVIEW|1|");
    CHECK(pos==0);noncanonical.replace(0,9,"SGVIEW|01|");
    expect_throw([&]{decode_frame(noncanonical);});
}
void failed_db_step_does_not_publish() {
    TempDb db;Simulation seed(1);auto ship=seed.add_ship(7,{0,0},10),rock=seed.add_asteroid({500,0},20);
    PersistentWorld w(db.file.string(),seed);ServerViewBridge srv(w.world(),8001);ClientView c;
    CHECK(c.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    CHECK(w.submit(mining(2,1,ship,rock,Action::StartMining,4)));
    expect_throw([&]{w.step(FailPoint::AfterSnapshotWrite);});
    CHECK(w.world().tick()==0 && w.world().find(ship)->cargo==0);
    CHECK(srv.sequence()==0 && c.world().tick==0);
    w.step(); auto d=round_trip(srv.after_committed_step(w.world()));
    CHECK(c.apply(d)==ApplyResult::Applied);
    CHECK(c.world().entities.at(ship).cargo==4);
}
void restarted_job_uses_new_epoch_no_old_notifications() {
    TempDb db;Simulation seed(3);auto ship=seed.add_ship(7,{0,0},20),rock=seed.add_asteroid({500,0},20);
    {
        PersistentWorld w(db.file.string(),seed);
        CHECK(w.submit(mining(3,1,ship,rock,Action::StartMining,4)));w.step();
        CHECK(w.world().find(ship)->cargo==4);
    }
    PersistentWorld w(db.file.string(),Simulation(50));ServerViewBridge restarted(w.world(),8100);ClientView c;
    CHECK(c.apply(round_trip(restarted.snapshot()))==ApplyResult::Applied);
    CHECK(c.recent_notifications().empty() && c.world().mining.count(ship));
    CHECK(c.world().entities.at(ship).cargo==4);
    w.step(); auto d=round_trip(restarted.after_committed_step(w.world()));
    CHECK(c.apply(d)==ApplyResult::Applied);
    CHECK(c.world().entities.at(ship).cargo==8);
    CHECK(count_activity(c,ActivityKind::AutoMiningTransfer,Outcome::Applied)==1);
}
void stale_snapshot_cannot_rewind_client() {
    Simulation s(1);s.add_ship(7,{0,0},10);ServerViewBridge srv(s,90001);ClientView c;
    const auto initial=round_trip(srv.snapshot());
    CHECK(c.apply(initial)==ApplyResult::Applied);
    s.step();auto delta=round_trip(srv.after_committed_step(s));
    CHECK(c.apply(delta)==ApplyResult::Applied);
    CHECK(c.apply(initial)==ApplyResult::NeedsSnapshot);
    CHECK(c.world().tick==1 && c.sequence()==1);
    auto conflicted=delta;conflicted.projection_hash^=2;
    expect_throw([&]{c.apply(conflicted);});
    CHECK(c.world().tick==1);
    CHECK(c.apply(round_trip(srv.snapshot()))==ApplyResult::DuplicateIgnored);
}
void empty_tick_has_contiguous_sequence() {
    Simulation s(5);s.add_ship(7,{0,0},30);ServerViewBridge srv(s,90002);ClientView c;
    CHECK(c.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    for(int i=1;i<=50;++i){
        s.step();const auto delta=round_trip(srv.after_committed_step(s));
        CHECK(delta.sequence==static_cast<std::uint64_t>(i));
        CHECK(delta.changes.empty());
        CHECK(c.apply(delta)==ApplyResult::Applied);
    }
    CHECK(c.world().tick==50 && c.sequence()==50);
}
void zero_ore_notified_as_remove_in_delta() {
    Simulation s(9);auto ship=s.add_ship(7,{0,0},20),rock=s.add_asteroid({500,0},10);
    ServerViewBridge srv(s,8200);ClientView c;
    CHECK(c.apply(round_trip(srv.snapshot()))==ApplyResult::Applied);
    CHECK(s.submit(mining(1,1,ship,rock,Action::Mine,10)));s.step();
    const auto d=round_trip(srv.after_committed_step(s));
    CHECK(c.apply(d)==ApplyResult::Applied);
    CHECK(!c.world().entities.count(rock));
    CHECK(count_activity(c,ActivityKind::AsteroidDespawned,Outcome::NoResources)==1);
    CHECK(c.world().entities.at(ship).cargo==10);
}

int main(){
    const std::vector<std::pair<const char*,std::function<void()>>> cases={
        {"snapshot wire roundtrip", start_snapshot},
        {"movement delta",ship_movement_delta},
        {"ten asteroid despawn through persisted client",ten_asteroids_remove_and_persist},
        {"automatic cargo full notification",automatic_full_stop_notifications},
        {"dropped delta and snapshot reconnect",dropped_packet_and_snapshot_reconnect},
        {"duplicate and stream epochs",duplicate_and_old_stream_rejected},
        {"state hash mismatch atomicity",forged_hash_and_invalid_frame_atomicity},
        {"malformed wire parser",malformed_wire_rejected},
        {"failed transaction not published",failed_db_step_does_not_publish},
        {"restarted live mining and no duplicate activity",restarted_job_uses_new_epoch_no_old_notifications},
        {"depleted entity remove notification",zero_ore_notified_as_remove_in_delta},
        {"stale snapshot cannot rewind",stale_snapshot_cannot_rewind_client},
        {"empty tick sequence continuity",empty_tick_has_contiguous_sequence}
    };
    for(const auto& [name,fn]:cases){try{fn();std::cout<<"PASS: "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL: "<<name<<": "<<e.what()<<'\n';return 1;}}
    std::cout << "B3 client contract checks passed: " << cases.size() << '/' << cases.size() << '\n';
}
