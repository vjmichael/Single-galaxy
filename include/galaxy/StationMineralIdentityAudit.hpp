#pragma once
// GROUP3-REF: read-only source-grounded StationRefinery -> INDUSTRY-01
// identity audit. A known legacy mineral name is NOT an approved production
// recipe or an automatic transfer into a workshop or ship cargo.
#include "galaxy/Industry01Catalog.hpp"
#include "galaxy/Simulation.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace galaxy::station_material_identity {
struct Row {
    std::string original_code;
    std::int64_t kilograms=0;
    std::int64_t grams=0;
    bool approved_industry_feed=false;
};
struct Report {
    EntityId station=0,source_ship=0;
    PlayerId owner=0;
    std::uint64_t last_refinery_receipt=0;
    std::int64_t processed_tonnes=0, stock_kilograms=0;
    std::vector<Row> stock;
};
inline std::optional<Report> audit_plant(const StationRefineryPlant& plant,
                                        EntityId station,PlayerId owner) {
    if(!owner || !station || plant.station!=station || plant.owner!=owner ||
       !plant.source_ship || plant.embodied_modules!=2 ||
       plant.processed_tonnes<0 || plant.processed_tonnes>1000000000)
        return std::nullopt;
    Report report{};
    report.station=station;report.source_ship=plant.source_ship;
    report.owner=owner;report.last_refinery_receipt=plant.last_receipt;
    report.processed_tonnes=plant.processed_tonnes;
    for(const auto& [name,kg]:plant.mineral_kg){
        const auto code=std::string("mineral/")+name;
        const auto& known=industry01::legacy_units();
        const auto it=std::find_if(known.begin(),known.end(),[&](const auto& u){
            return u.code==code;
        });
        if(it==known.end() || it->kind!="kilogram" ||
           it->mass_g_per_unit!=1000 || kg<=0 ||
           kg>std::numeric_limits<std::int64_t>::max()/1000 ||
           report.stock_kilograms>std::numeric_limits<std::int64_t>::max()-kg)
            return std::nullopt;
        report.stock.push_back({code,kg,kg*1000,false});
        report.stock_kilograms+=kg;
    }
    if(report.stock_kilograms>report.processed_tonnes*1000 ||
       (report.processed_tonnes==0 && report.stock_kilograms>0))
        return std::nullopt;
    return report;
}
inline std::optional<Report> audit_original_station(const Simulation& world,
                                                     EntityId station,PlayerId owner){
    const auto* plant=world.station_refinery(station);
    return plant?audit_plant(*plant,station,owner):std::nullopt;
}
} // namespace galaxy::station_material_identity
