#include "galaxy/Galaxy.hpp"
#include <iostream>
#include <string>
using namespace galaxy;
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: galaxy_survey_demo SURVEY_DB_PATH\n";return 2;}
    try {
        GalaxyMap map;
        map.add_system({1,"Foundry",90,Sovereignty::NPC});
        map.add_system({2,"Cutwater",15,Sovereignty::NPC});
        map.add_system({3,"New Frontier",0,Sovereignty::Unclaimed});
        map.add_system({4,"Long Route",80,Sovereignty::NPC});
        map.link(1,2);map.link(2,3);map.link(1,4);map.link(4,3);
        map.add_site(3,{101,SiteKind::Star,{0,0},"Frontier Star",true,false,0});
        map.add_site(3,{102,SiteKind::Planet,{50000,60000},"World-1",true,false,0});
        map.add_site(3,{103,SiteKind::GasCloud,{5000,1000},"Gas reserve",false,false,30});
        map.add_site(3,{104,SiteKind::Ship,{5000,-6000},"Unknown vessel",false,true,0});
        SurveySensors scanner(map);scanner.register_probe(1,7);
        const auto fast=map.route(1,3,RoutePolicy::Shortest);
        const auto safe=map.route(1,3,RoutePolicy::PreferSecure);
        auto print_route=[](const RouteResult& r){for(auto id:r.systems)std::cout<<' '<<id;};
        std::cout<<"Shortest route:";print_route(fast);std::cout<<"\nRisk-averse route:";print_route(safe);std::cout<<'\n';
        std::cout<<"On entry: "<<map.visible_on_entry(3).size()<<" major bodies visible\n";
        std::cout<<"Directional scan: "<<scanner.directional(3,{0,0},-50,70,10000).size()<<" nearby ship contact(s)\n";
        scanner.deploy(1,7,3,{5000,1000});
        auto observations=scanner.probe_scan(7,3,42,1500,60);
        SurveyArchive archive(argv[1]);
        for(auto& obs:observations){obs.published=true;archive.record(obs);}
        scanner.recover(1,7,3);
        std::cout<<"New probe discoveries: "<<observations.size()<<"; now stored in SQLite\n";
        std::cout<<"Published records: "<<archive.published(3).size()<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
