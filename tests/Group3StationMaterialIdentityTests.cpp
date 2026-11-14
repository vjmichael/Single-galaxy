#include "galaxy/StationMineralIdentityAudit.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("Group3 resource identity line ")+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
 try{
    industry01::validate_against_original_inventory();
    StationRefineryPlant plant{};
    plant.station=30;plant.source_ship=8;plant.owner=7;
    plant.embodied_modules=2;plant.assigned_workers=1;
    plant.processed_tonnes=1;plant.last_receipt=97;
    plant.mineral_kg={{"nickel_iron",860},{"troilite",80},
       {"olivine",30},{"pyroxene",15},{"other",15}};
    const auto before=plant.mineral_kg;
    const auto audit=station_material_identity::audit_plant(plant,30,7);
    CHECK(audit && audit->stock_kilograms==1000 && audit->stock.size()==5);
    CHECK(audit->last_refinery_receipt==97 && audit->processed_tonnes==1);
    for(const auto& row:audit->stock){
        CHECK(row.kilograms>0 && row.grams==row.kilograms*1000);
        CHECK(!row.approved_industry_feed);
    }
    CHECK(plant.mineral_kg==before);
    CHECK(!station_material_identity::audit_plant(plant,30,8));
    CHECK(!station_material_identity::audit_plant(plant,31,7));
    plant.mineral_kg["nickel_iron"]=1001;
    CHECK(!station_material_identity::audit_plant(plant,30,7));
    plant.mineral_kg=before;
    plant.mineral_kg["nickel_iron"]=-1;
    CHECK(!station_material_identity::audit_plant(plant,30,7));
    plant.mineral_kg=before;
    plant.mineral_kg["invented_unresearched_material"]=4;
    CHECK(!station_material_identity::audit_plant(plant,30,7));
    CHECK(industry01::is_native_workshop_recipe("native_cut_aluminum_panel"));
    CHECK(industry01::is_native_workshop_recipe("native_shape_aluminum_frame"));
    CHECK(!industry01::is_native_workshop_recipe("smelt_nickel_iron"));
    std::cout<<"GROUP3 RESOURCE IDENTITY PASS: 5 original station mineral codes, exact 1000kg, no invented candidate conversion, station ownership, fail-closed units and mass\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
