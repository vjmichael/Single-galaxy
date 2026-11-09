#include "galaxy/StationRefineryBatch.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::station_refining;
#define CHECK(x) do { if(!(x))throw std::runtime_error(std::string("PLAY-STATION-03A line ")+std::to_string(__LINE__)+": " #x); } while(false)
static std::int64_t kilograms(const Facility& p) {
    std::int64_t sum=0;
    for(const auto& entry:p.mineral_kg) sum+=entry.second;
    return sum;
}
int main() {
 try {
    StationHangar hangar{1,8};
    OreLot lot{17,91,7,200,AsteroidClass::Metallic,1};
    Facility plant{};
    plant.station=17;plant.source_ship=18;plant.owner=7;
    plant.installed_modules=2;plant.assigned_workers=1;plant.power_units=2;
    CHECK(refine_one_tonne(hangar,lot,plant,8,17,1,201).status==Status::WrongOwner);
    CHECK(refine_one_tonne(hangar,lot,plant,7,16,1,201).status==Status::WrongOwner);
    auto bad=lot;bad.unload_receipt=0;
    CHECK(refine_one_tonne(hangar,bad,plant,7,17,1,201).status==Status::InvalidLot);
    auto missing=plant;missing.installed_modules=0;
    CHECK(refine_one_tonne(hangar,lot,missing,7,17,1,201).status==Status::NoFacility);
    auto unstaffed=plant;unstaffed.assigned_workers=0;
    CHECK(refine_one_tonne(hangar,lot,unstaffed,7,17,1,201).status==Status::NoWorker);
    auto dark=plant;dark.power_units=0;
    CHECK(refine_one_tonne(hangar,lot,dark,7,17,1,201).status==Status::NoPower);
    CHECK(hangar.ore==1 && lot.remaining_tonnes==1 && kilograms(plant)==0);
    CHECK(refine_one_tonne(hangar,lot,plant,7,17,1,201).status==Status::Applied);
    CHECK(hangar.ore==0 && lot.remaining_tonnes==0);
    CHECK(plant.mineral_kg.at("nickel_iron")==860);
    CHECK(plant.mineral_kg.at("troilite")==80);
    CHECK(plant.mineral_kg.at("olivine")==30);
    CHECK(plant.mineral_kg.at("pyroxene")==15);
    CHECK(plant.mineral_kg.at("other")==15);
    CHECK(kilograms(plant)==1000 && plant.processed_tonnes==1);
    CHECK(plant.power_units==1 && plant.ready_tick==5 && plant.last_receipt==201);
    CHECK(refine_one_tonne(hangar,lot,plant,7,17,2,201).status==Status::Replay);
    CHECK(refine_one_tonne(hangar,lot,plant,7,17,2,202).status==Status::CoolingDown);
    CHECK(refine_one_tonne(hangar,lot,plant,7,17,5,202).status==Status::NoOre);
    CHECK(kilograms(plant)==1000 && hangar.ore==0 && plant.power_units==1);
    // A second physically separate, geology-tagged lot at the SAME station
    // cannot reuse the original unload receipt or steal somebody else's ore.
    StationHangar second_hangar{1,8};
    OreLot second{17,92,7,204,AsteroidClass::Silicate,1};
    CHECK(refine_one_tonne(second_hangar,second,plant,7,17,5,205).status==Status::Applied);
    CHECK(second_hangar.ore==0 && second.remaining_tonnes==0);
    CHECK(plant.mineral_kg.at("plagioclase")==400);
    CHECK(plant.processed_tonnes==2 && plant.power_units==0);
    CHECK(kilograms(plant)==2000);
    CHECK(refine_one_tonne(second_hangar,second,plant,7,17,10,206).status==Status::NoPower);
    // Both batches have 2,000kg in physical station mineral stock; none
    // appears aboard a ship. Credits and workers are not generated here.
    std::cout<<"PLAY-STATION-03A PASS: 2 original geology profiles, one-tonne lot, independent station mineral kg, no free plant/workers/power, replay/cooldown/ownership denial, full mass conservation\n";
    return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
