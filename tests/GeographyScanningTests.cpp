#include "galaxy/Galaxy.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <algorithm>

using namespace galaxy;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(std::string("assert failed: ")+#x+" line "+std::to_string(__LINE__));}while(0)

static GalaxyMap make_map() {
    GalaxyMap map;
    map.add_system({1,"Home",95,Sovereignty::NPC});
    map.add_system({2,"Unsafe Shortcut",5,Sovereignty::Unclaimed});
    map.add_system({3,"Frontier",0,Sovereignty::Player});
    map.add_system({4,"Protected Corridor",85,Sovereignty::NPC});
    map.add_system({5,"Border",75,Sovereignty::NPC});
    map.link(1,2);map.link(2,3);map.link(1,4);map.link(4,5);map.link(5,3);
    map.add_site(3,{101,SiteKind::Star,{0,0},"Star",true,false,0});
    map.add_site(3,{102,SiteKind::Planet,{8000,0},"Planet",true,false,0});
    map.add_site(3,{103,SiteKind::Ship,{10000,10000},"Unknown ship",false,true,0});
    map.add_site(3,{104,SiteKind::GasCloud,{9000,2000},"Hidden gas",false,false,35});
    map.add_site(3,{105,SiteKind::Wormhole,{-9000,0},"Signature",false,false,75});
    return map;
}
static void geography_routes() {
    auto map=make_map();
    CHECK(map.system(3).sovereignty==Sovereignty::Player && map.system(3).security==0);
    CHECK((map.route(1,3,RoutePolicy::Shortest).systems==std::vector<std::uint32_t>{1,2,3}));
    CHECK((map.route(1,3,RoutePolicy::PreferSecure).systems==std::vector<std::uint32_t>{1,4,5,3}));
    map.set_link_operational(4,5,false);
    CHECK((map.route(1,3,RoutePolicy::PreferSecure).systems==std::vector<std::uint32_t>{1,2,3}));
    map.set_link_operational(2,3,false);
    CHECK(map.route(1,3,RoutePolicy::Shortest).systems.empty());
    CHECK(map.visible_on_entry(3).size()==2);
    bool threw=false;try{map.add_site(3,{104,SiteKind::GasCloud,{0,0},"duplicate",false,false,50});}catch(const std::invalid_argument&){threw=true;}
    CHECK(threw);
    std::cout<<"PASS secure vs shortest routing, gate shutdown, no-route, independent sovereignty, entry knowledge\n";
}
static void scanning_and_probes() {
    const auto map=make_map();SurveySensors sensors(map);
    const auto narrow=sensors.directional(3,{0,0},45,30,20000);
    CHECK(narrow.size()==1 && narrow[0].site==103 && narrow[0].bearing_deg==45);
    CHECK(narrow[0].range_band_mm==14000); // coarse only
    CHECK(sensors.directional(3,{0,0},220,30,20000).empty());
    CHECK(sensors.directional(3,{0,0},45,30,5000).empty());
    CHECK(sensors.probe_scan(7,3,1,4000,90).empty());
    auto contacts= sensors.coarse_signatures(7,3,{0,0},20000,40);
    CHECK(contacts.size()==1 && contacts[0].site==104);
    CHECK(contacts[0].coarse_center.x==10000 && contacts[0].coarse_center.y==0);
    CHECK(contacts[0].uncertainty_radius_mm==5000);
    CHECK(sensors.coarse_signatures(7,3,{0,0},20000,20).size()==1);
    CHECK(sensors.coarse_signatures(7,3,{0,0},20000,10).empty());
    sensors.register_probe(99,7);sensors.register_probe(100,7);
    CHECK(!sensors.deploy(99,8,3,{9000,2000}));
    CHECK(sensors.deploy(99,7,3,{9000,2000}));
    CHECK(!sensors.plan_refinement(8,3,104,2000,40).has_value());
    CHECK(!sensors.plan_refinement(7,3,105,2000,40).has_value());
    auto plan=sensors.plan_refinement(7,3,104,2000,40);
    CHECK(plan.has_value() && plan->site==104 && plan->waypoints.size()==1);
    CHECK(plan->waypoints[0].first==99);
    CHECK(!sensors.deploy(99,7,3,{9000,2000}));
    auto first=sensors.probe_scan(7,3,4,1000,40);
    CHECK(first.size()==1 && first[0].site==104 && first[0].confidence>=50);
    CHECK(first[0].estimated_position.x==10000 && first[0].estimated_position.y==2500);
    CHECK(sensors.probe_scan(8,3,4,1000,90).empty());
    CHECK(sensors.deploy(100,7,3,{9000,2000}));
    auto dual=sensors.plan_refinement(7,3,104,2000,40);
    CHECK(dual.has_value() && dual->waypoints.size()==2);
    CHECK(dual->waypoints[0].second.x!=dual->waypoints[1].second.x);
    CHECK(sensors.probe_scan(7,3,5,1000,40)[0].confidence==first[0].confidence);
    auto env=sensors.scan_mode(7,3,5,1000,40,ScanMode::Environmental);
    CHECK(env.size()==1 && env[0].site==104);
    CHECK(sensors.scan_mode(7,3,5,1000,40,ScanMode::Celestial).empty());
    CHECK(sensors.scan_mode(7,3,5,1000,40,ScanMode::Anomaly).empty());
    CHECK(sensors.scan_mode(8,3,5,1000,40,ScanMode::Environmental).empty());
    CHECK(sensors.recover(100,7,3));
    CHECK(sensors.deploy(100,7,3,{9500,2000}));
    auto second=sensors.probe_scan(7,3,5,1000,40);
    CHECK(second.size()==1 && second[0].confidence>first[0].confidence);
    CHECK(second[0].estimated_position.x==9000 && second[0].estimated_position.y==2000);
    CHECK(sensors.probe_scan(7,3,6,1000,80).size()==1); // wormhole outside the probe's coverage
    CHECK(!sensors.recover(99,8,3));CHECK(sensors.recover(99,7,3));
    CHECK(sensors.recover(100,7,3));CHECK(sensors.deployed_count(7)==0);
    CHECK(sensors.probe_scan(7,3,7,1000,100).empty());
    CHECK(sensors.deploy(99,7,3,{-9000,0}));
    CHECK(sensors.probe_scan(7,3,8,1000,70).empty());
    CHECK(sensors.probe_scan(7,3,9,1000,85).size()==1);
    auto anomaly=sensors.scan_mode(7,3,9,1000,85,ScanMode::Anomaly);
    CHECK(anomaly.size()==1 && anomaly[0].site==105);
    CHECK(sensors.scan_mode(7,3,9,1000,85,ScanMode::Environmental).empty());
    std::cout<<"PASS directional geometry, ownership, strength, probe recovery and hidden wormhole\n";
}
static void astronomy_profiles() {
    auto map=make_map();
    const auto first=map.astronomy(3,123456);
    const auto again=map.astronomy(3,123456);
    CHECK(first.architecture==again.architecture);
    CHECK(first.morphology==again.morphology);
    CHECK(first.star_count==again.star_count);
    CHECK(first.features==again.features);
    CHECK(first.generator_version==1);
    std::set<StellarArchitecture> architectures;
    for(std::uint32_t system=1;system<1000;++system) {
        const auto p=generate_astronomical_profile(123456,system);
        architectures.insert(p.architecture);
        CHECK(p.star_count>=1 && p.star_count<=3);
        CHECK(p.architecture!=StellarArchitecture::Single || p.star_count==1);
        CHECK(p.morphology!=StarMorphology::CloseBinaryTidal || p.star_count>1);
    }
    CHECK(architectures.size()==4);
    std::cout<<"PASS deterministic stellar structure and unusual space features\\n";
}
static void archive_persistence() {
    const auto file=std::filesystem::temp_directory_path()/"galaxy-b60-survey-tests.sqlite";
    std::filesystem::remove(file);
    try {
        {
            SurveyArchive archive(file.string());
            ScanObservation p{7,3,104,SiteKind::GasCloud,{9000,2000},11,55,false};
            CHECK(archive.record(p));CHECK(!archive.record(p));
            CHECK(archive.history(7,3).size()==1);
            CHECK(archive.history(7,3)[0].observed_tick==11);
            CHECK(archive.personal(7,3).size()==1 && archive.published(3).empty());
            CHECK(archive.known_site(7,3,104).has_value());
            CHECK(!archive.known_site(8,3,104).has_value());
            CHECK(!archive.known_site(7,3,105).has_value());
            CHECK(archive.known_sites(7,3,56).empty());
            CHECK(archive.known_sites(7,3,55).size()==1);
            p.observed_tick=10;p.confidence=95;p.published=true;
            CHECK(!archive.record(p)); // stale data cannot overwrite newer truth
            p.observed_tick=12;p.confidence=70;p.published=true;
            CHECK(archive.record(p));
            CHECK(archive.history(7,3).size()==2); // older survey not overwritten
            CHECK(archive.history(7,3)[0].observed_tick==12);
            CHECK(archive.history(7,3)[1].observed_tick==11);
            CHECK(archive.history(7,3,1).size()==1);
            CHECK(archive.history(9,3).empty()); // private history
            bool invalid=false;
            try{(void)archive.history(7,3,101);}catch(const std::invalid_argument&){invalid=true;}
            CHECK(invalid);
            auto pub=archive.published(3);
            CHECK(pub.size()==1 && pub[0].independent_surveyors==1 && pub[0].confidence==70);
            auto another=p;another.player=8;another.observed_tick=13;another.confidence=80;
            CHECK(archive.record(another));
            // Public records cannot be silently made private by a later private scan.
            auto refreshed=p;refreshed.observed_tick=14;refreshed.published=false;
            CHECK(archive.record(refreshed));
            CHECK(archive.personal(7,3)[0].published);
            CHECK(archive.history(7,3)[0].published);
            CHECK(archive.history(7,3).size()==3);
            pub=archive.published(3);
            CHECK(pub.size()==1 && pub[0].independent_surveyors==2 && pub[0].confidence==80);
            // Private observations do not affect public confidence.
            another.player=9;another.published=false;
            CHECK(archive.record(another));
            CHECK(archive.published(3)[0].independent_surveyors==2);
        }
        {
            SurveyArchive restored(file.string());
            CHECK(restored.personal(7,3).size()==1);
            CHECK(restored.known_site(7,3,104).has_value());
            CHECK(restored.known_site(7,3,104)->observed_tick==14);
            CHECK(restored.known_site(7,3,104)->published);
            CHECK(restored.known_sites(7,3,71).empty()); // latest private snapshot confidence is 70
            CHECK(!restored.known_site(10,3,104).has_value());
            CHECK(restored.personal(7,3)[0].observed_tick==14);
            CHECK(restored.history(7,3).size()==4);
            CHECK(restored.history(7,3)[1].observed_tick==14);
            CHECK(restored.history(7,3)[2].observed_tick==12);
            // Reports are durable across disconnection, and replay is idempotent.
            const ScanObservation delayed{7,3,105,SiteKind::Wormhole,{-9000,0},15,88,false};
            CHECK(restored.queue_probe_report(delayed,1000));
            CHECK(!restored.queue_probe_report(delayed,1000));
            CHECK(!restored.known_site(7,3,105).has_value());
            CHECK(restored.deliver_probe_reports(7,10999,0)==0);
            CHECK(restored.deliver_probe_reports(7,11000,SurveyArchive::kDeepProbeLinkMm+1)==0);
            CHECK(restored.deliver_probe_reports(8,11000,0)==0);
            CHECK(restored.deliver_probe_reports(7,11000,SurveyArchive::kDeepProbeLinkMm)==1);
            CHECK(restored.deliver_probe_reports(7,11000,0)==0);
            CHECK(restored.known_site(7,3,105)->confidence==88);
            CHECK(!restored.known_site(8,3,105).has_value());
            CHECK(restored.published(3).size()==1);
            auto current=make_map();
            // An archived position is knowledge, not construction permission.
            CHECK(restored.actionable_sites(7,3,current,DiscoveryUse::Navigation,50,20,10).empty());
            CHECK(restored.record({7,3,102,SiteKind::Planet,{8000,0},30,90,false}));
            auto route=restored.actionable_sites(7,3,current,DiscoveryUse::Navigation,80,30,2);
            CHECK(route.size()==1 && route[0].site==102);
            CHECK(restored.actionable_sites(8,3,current,DiscoveryUse::Navigation,80,30,2).empty());
            CHECK(restored.actionable_sites(7,3,current,DiscoveryUse::Navigation,80,50,2).empty());
            CHECK(restored.actionable_sites(7,3,current,DiscoveryUse::SettlementSurvey,80,30,2).size()==1);
            CHECK(restored.actionable_sites(7,3,current,DiscoveryUse::SettlementSurvey,95,30,2).empty());
            CHECK(restored.published(3)[0].independent_surveyors==2);
        }
    }catch(...){std::filesystem::remove(file);throw;}
    std::filesystem::remove(file);
    std::cout<<"PASS durable per-player survey history, private/public isolation, deduplicated contributions\n";
}
int main(){try{geography_routes();scanning_and_probes();astronomy_profiles();archive_persistence();std::cout<<"B5.4/B6.0 ALL PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
