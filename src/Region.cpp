#include "galaxy/Region.hpp"
#include <array>
#include <stdexcept>

namespace galaxy {
EntityId RegionState::gate(std::uint32_t from,std::uint32_t to) const {
    auto it=directed_gates.find({from,to});
    return it==directed_gates.end()?0:it->second;
}
GalaxyMap RegionState::operational_routes(const Simulation& world) const {
    GalaxyMap routing;
    for(std::uint32_t id=1;id<=7;++id)routing.add_system(map.system(id));
    // Authoritative physical gate status, rather than merely a theoretical link.
    for(const auto& [source,target]:directed_gates) {
        if(source.first>source.second)continue;
        const auto* a=world.find(target);
        const auto* b=world.find(gate(source.second,source.first));
        if(!a||!b||a->type!=EntityType::Stargate||b->type!=EntityType::Stargate ||
           a->system_id!=source.first||b->system_id!=source.second||
           a->gate_destination!=source.second||b->gate_destination!=source.first)
            throw std::logic_error("region gate topology does not match committed world");
        routing.link(source.first,source.second,a->gate_online&&b->gate_online);
    }
    return routing;
}
RegionState build_lumen_reach(Simulation& world,EntityId home,EntityId frontier,
    EntityId rock_home,EntityId rock_frontier,EntityId gate12,EntityId gate21) {
    RegionState result;
    for(auto sys:std::array<SystemDescriptor,7>{{
        {1,"Haven",85,Sovereignty::NPC},
        {2,"Sable Gap",30,Sovereignty::Unclaimed},
        {3,"Lumenford",88,Sovereignty::NPC},
        {4,"Glass Meridian",72,Sovereignty::NPC},
        {5,"Driftwater",68,Sovereignty::NPC},
        {6,"Cinder Cut",8,Sovereignty::Unclaimed},
        {7,"Far Lantern",25,Sovereignty::Player}
    }}) result.map.add_system(sys);
    result.stations[1]=home;result.stations[2]=frontier;
    result.asteroids[1]=rock_home;result.asteroids[2]=rock_frontier;
    for(std::uint32_t sys=3;sys<=7;++sys) {
        world.add_system(sys);
        result.stations[sys]=world.add_station_in_system(sys,{24000,-12000});
        result.asteroids[sys]=world.add_asteroid_in_system(sys,{25500,-11200},90+int(sys)*20);
        world.configure_market(result.stations[sys],int(sys)+3,1200);
    }
    // Existing B6.3 first two survey catalogs are stable for regression.
    result.map.add_site(1,{101,SiteKind::Star,{30000,30000},"Haven primary",true,false,0});
    result.map.add_site(1,{102,SiteKind::Planet,{-20000,-15000},"Haven outer planet",true,false,0});
    result.map.add_site(1,{103,SiteKind::GasCloud,{9000,2000},"Haven gas",false,false,35});
    result.map.add_site(1,{104,SiteKind::Wormhole,{-9000,3000},"Haven anomaly",false,false,75});
    result.map.add_site(2,{201,SiteKind::Star,{26000,26000},"Sable primary",true,false,0});
    result.map.add_site(2,{202,SiteKind::Planet,{-25000,13000},"Sable planet",true,false,0});
    result.map.add_site(2,{203,SiteKind::GasCloud,{22000,-8000},"Sable gas",false,false,50});
    result.map.add_site(2,{204,SiteKind::Anomaly,{25000,5000},"Sable anomaly",false,false,70});
    for(std::uint32_t sys=3;sys<=7;++sys) {
        result.map.add_site(sys,{sys*100+1,SiteKind::Star,{30000,30000},"Primary star",true,false,0});
        result.map.add_site(sys,{sys*100+2,SiteKind::Planet,{-21000,16000},"Outer planet",true,false,0});
        result.map.add_site(sys,{sys*100+3,SiteKind::GasCloud,{6500,3500},"Unmapped gas",false,false,25+int(sys)*6});
        result.map.add_site(sys,{sys*100+4,SiteKind::Anomaly,{-15000,9000},"Unmapped anomaly",false,false,50+int(sys)*4});
    }
    const auto install=[&](std::uint32_t a,std::uint32_t b,Vector2 pa,Vector2 pb) {
        // Both endpoints are real world entities; arrival position is in the
        // destination partition, where Simulation applies its 12km arrival offset.
        auto ab=world.add_gate(a,pa,b,pb);
        auto ba=world.add_gate(b,pb,a,pa);
        result.directed_gates[{a,b}]=ab;
        result.directed_gates[{b,a}]=ba;
        result.map.link(a,b);
    };
    result.directed_gates[{1,2}]=gate12;result.directed_gates[{2,1}]=gate21;
    result.map.link(1,2);
    install(1,3,{-14000,10000},{0,0});
    install(2,6,{-2000,15000},{-12000,0});
    install(6,7,{9000,1000},{-10000,0});
    install(3,4,{16000,2000},{-10000,-10000});
    install(4,5,{15000,-10000},{-12000,-5000});
    install(5,7,{11000,8000},{10000,9000});
    install(2,4,{8000,-16000},{2000,16000});
    install(4,6,{-1000,18000},{14000,-12000});
    (void)result.operational_routes(world); // reject mismatched physical topology at startup
    return result;
}
} // namespace galaxy
