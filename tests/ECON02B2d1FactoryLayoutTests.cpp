#include "galaxy/FactoryLayout.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
using namespace galaxy::industry;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("B2d1 line ")+std::to_string(__LINE__)+": " #x);} while(0)
template<class F> static void reject(F&& fn){
    bool caught=false;try{fn();}catch(const std::exception&){caught=true;}
    CHECK(caught);
}
static FactoryLayout layout(){
    FactoryLayout f;
    f.place_storage(1,{0,0},2,2,4000);
    f.place_junction(2,DeviceKind::Splitter,{5,0});
    f.place_machine(3,{10,0},1,{"good/aluminum"},
                    {"good/aluminum_panels","good/aluminum_scrap"});
    f.place_junction(4,DeviceKind::Filter,{5,5},"good/aluminum_scrap");
    f.place_storage(5,{10,5},2,2,4000);
    f.add_conveyor(101,1,2,"good/aluminum",7,{{2,0},{3,0},{4,0}});
    f.add_conveyor(102,2,3,"good/aluminum",5,{{6,0},{7,0},{8,0},{9,0}});
    f.add_conveyor(103,3,5,"good/aluminum_panels",5,{{10,2},{10,3},{10,4}});
    f.add_conveyor(104,2,4,"good/aluminum_scrap",3,{{5,1},{5,2},{5,3},{5,4}});
    f.add_conveyor(105,4,5,"good/aluminum_scrap",3,{{6,5},{7,5},{8,5},{9,5}});
    return f;
}
int main(){
 try {
    auto f=layout();
    CHECK(f.devices().size()==5 && f.conveyors().size()==5);
    CHECK(f.devices().at(3).width==2 && f.devices().at(3).height==2);
    const auto original=f.canonical_blueprint();
    const auto loaded=FactoryLayout::from_blueprint(original);
    CHECK(loaded.canonical_blueprint()==original);
    auto n=f.compile_transport();
    n.seed_from_authoritative_inventory(1,"good/aluminum",30);
    n.seed_from_authoritative_inventory(2,"good/aluminum_scrap",9);
    const auto initial=n.total_stacks();
    CHECK(n.advance(1)==18); // aluminum 7 + 5; scrap 3 + 3
    CHECK(n.node(1).amount("good/aluminum")==23);
    CHECK(n.node(2).amount("good/aluminum")==2);
    CHECK(n.node(3).amount("good/aluminum")==5);
    CHECK(n.node(5).amount("good/aluminum_panels")==0); // no magic production
    CHECK(n.node(5).amount("good/aluminum_scrap")==3);
    CHECK(n.total_stacks()==initial); // all transport conserved
    // Same logical route order, distant single advance versus exact ticks.
    auto remote=f.compile_transport(),near=f.compile_transport();
    for(auto* p:{&remote,&near}){
        p->seed_from_authoritative_inventory(1,"good/aluminum",30);
        p->seed_from_authoritative_inventory(2,"good/aluminum_scrap",9);
    }
    remote.advance(100);
    for(int i=0;i<100;++i)near.advance(1);
    CHECK(remote.tick()==near.tick());
    CHECK(remote.total_stacks()==near.total_stacks());
    for(auto id:{1ULL,2ULL,3ULL,4ULL,5ULL})
        CHECK(remote.node(id).stacks()==near.node(id).stacks());
    for(auto id:{101ULL,102ULL,103ULL,104ULL,105ULL})
        CHECK(remote.route(id).units_moved==near.route(id).units_moved);
    CHECK(remote.total_stacks()==initial);
    // Workshop recipe ports stay explicit; filter cannot be bypassed.
    reject([&]{f.add_conveyor(106,2,4,"good/aluminum",2,{{5,1},{5,2},{5,3},{5,4}});});
    reject([&]{f.add_conveyor(106,3,5,"good/aluminum",2,{{10,2},{10,3},{10,4}});});
    reject([&]{f.place_storage(6,{10,1},2,2,5000);});  // collides machine
    reject([&]{f.place_storage(6,{7,0},1,1,5000);});   // collides conveyor
    reject([&]{f.add_conveyor(106,1,2,"good/aluminum",2,{{2,0},{3,1},{4,0}});});
    reject([&]{f.add_conveyor(106,1,2,"good/aluminum",2,{{2,0},{3,0},{3,0},{4,0}});});
    reject([&]{f.add_conveyor(106,1,2,"good/aluminum",2,{{100,0}});});
    reject([&]{f.add_conveyor(106,1,1,"good/aluminum",2,{{2,0}});});
    reject([&]{f.add_conveyor(106,3,1,"good/aluminum_panels",2,
                 {{10,-1},{9,-1},{8,-1},{7,-1},{6,-1},{5,-1},{4,-1},{3,-1},{2,-1},{1,-1},{0,-1}});}); // cycle
    reject([&]{f.place_machine(7,{3,0},0,{"good/aluminum"},{"good/aluminum_panels"});});
    reject([&]{f.place_machine(7,{3,0},6,{"good/aluminum"},{"good/aluminum_panels"});});
    reject([&]{f.place_machine(7,{3,0},1,{"good/magic"},{"good/aluminum_panels"});});
    reject([&]{f.place_junction(7,DeviceKind::Filter,{0,10},"good/magic");});
    CHECK(f.canonical_blueprint()==original); // failed planning calls are atomic
    // A compact storage link compiled from a placed route preserves exact
    // volumes even when many remote ticks are advanced at once.
    FactoryLayout tight;
    tight.place_storage(11,{0,0},1,1,1000);
    tight.place_storage(12,{5,0},1,1,2);
    tight.add_conveyor(200,11,12,"good/aluminum",4,{{1,0},{2,0},{3,0},{4,0}});
    auto pipeline=tight.compile_transport();
    pipeline.seed_from_authoritative_inventory(11,"good/aluminum",50);
    CHECK(pipeline.advance(1000000000ULL)==2);
    CHECK(pipeline.node(11).amount("good/aluminum")==48);
    CHECK(pipeline.node(12).amount("good/aluminum")==2);
    CHECK(pipeline.advance(1000000000ULL)==0); // backpressure
    CHECK(pipeline.total_stacks().at("good/aluminum")==50);
    // No designer-imposed room size or area tier: 32x32 machine far from origin.
    f.place_machine(99,{999999,-999999},5,{"good/aluminum"},{"good/aluminum_frames"});
    CHECK(f.devices().at(99).width==32);
    CHECK(FactoryLayout::from_blueprint(f.canonical_blueprint()).canonical_blueprint()==f.canonical_blueprint());
    // Malformed, noncanonical, and spatially impossible blueprints fail closed.
    reject([&]{FactoryLayout::from_blueprint("single-galaxy-factory-layout-v2\n");});
    reject([&]{FactoryLayout::from_blueprint(original.substr(0,original.size()-1));});
    std::string tamper=original;const auto pos=tamper.find("C|101|1|2|good/aluminum|7|2:0");
    CHECK(pos!=std::string::npos);
    // Change a valid first conveyor tile into one intersecting a storage device.
    const auto marker=std::string("good/aluminum|7|2:0");
    const auto position=tamper.find(marker);
    CHECK(position!=std::string::npos);
    tamper.replace(position+marker.size()-3,3,"1:0");
    reject([&]{FactoryLayout::from_blueprint(tamper);});
    std::cout<<"ECON-02B2d1 spatial layout, ports, conveyor validation, deterministic logistics PASS\n";
    return 0;
 }catch(const std::exception& ex){std::cerr<<"ECON-02B2d1 FAIL "<<ex.what()<<'\n';return 1;}
}
