#include "galaxy/PlanetaryAgriculture.hpp"
#include <limits>

namespace galaxy::planetary {
namespace {
bool nonnegative(const Stock& s) {
    return s.water>=0 && s.nutrients>=0 && s.food>=0 && s.wastewater>=0 &&
           s.organic_waste>=0 && s.compost>=0;
}
bool safe_sum(std::int64_t a, std::int64_t b) {
    return b>=0 && a>=0 && a<=std::numeric_limits<std::int64_t>::max()-b;
}
bool move(Stock& from, Stock& to, Material m, std::int64_t kg) {
    if(kg<=0 || from.at(m)<kg || !safe_sum(to.at(m),kg)) return false;
    from.at(m)-=kg;
    to.at(m)+=kg;
    return true;
}
bool fits(std::int64_t batch, std::int64_t per) {
    return batch>0 && batch<=std::numeric_limits<std::int64_t>::max()/per;
}
}
std::int64_t Stock::total() const {
    std::int64_t sum=0;
    for(const auto v:{water,nutrients,food,wastewater,organic_waste,compost}) {
        if(!safe_sum(sum,v)) throw std::overflow_error("planetary mass overflow");
        sum+=v;
    }
    return sum;
}
std::int64_t& Stock::at(Material m) {
    switch(m) {
        case Material::Water: return water;
        case Material::Nutrients: return nutrients;
        case Material::Food: return food;
        case Material::Wastewater: return wastewater;
        case Material::OrganicWaste: return organic_waste;
        case Material::Compost: return compost;
    }
    throw std::invalid_argument("unknown material");
}
const std::int64_t& Stock::at(Material m) const {
    return const_cast<Stock*>(this)->at(m);
}
void MaterialCustody::genesis_site(SiteId body,std::uint64_t owner,const Stock& s) {
    if(!body || !owner || !nonnegative(s) || sites_.count(body)) throw std::invalid_argument("invalid/duplicate site genesis");
    s.total();
    sites_.emplace(body,Site{body,owner,s,0});
}
void MaterialCustody::genesis_ship(std::uint64_t ship,const Stock& s) {
    if(!ship || !nonnegative(s) || ships_.count(ship)) throw std::invalid_argument("invalid/duplicate ship genesis");
    s.total();
    ships_.emplace(ship,s);
}
const Site& MaterialCustody::site(SiteId body) const {return sites_.at(body);}
const Stock& MaterialCustody::ship(std::uint64_t ship) const {return ships_.at(ship);}
std::int64_t MaterialCustody::total_mass() const {
    std::int64_t sum=0;
    for(const auto& p:sites_) {
        auto mass=p.second.stock.total();
        if(!safe_sum(sum,mass)) throw std::overflow_error("planetary total overflow");
        sum+=mass;
    }
    for(const auto& p:ships_) {
        auto mass=p.second.total();
        if(!safe_sum(sum,mass)) throw std::overflow_error("planetary total overflow");
        sum+=mass;
    }
    return sum;
}
bool MaterialCustody::transfer_ship_to_site(std::uint64_t ship,SiteId body,Material m,std::int64_t kg) {
    auto a=ships_.find(ship);auto b=sites_.find(body);
    return a!=ships_.end() && b!=sites_.end() && move(a->second,b->second.stock,m,kg);
}
bool MaterialCustody::transfer_site_to_ship(SiteId body,std::uint64_t ship,Material m,std::int64_t kg) {
    auto a=sites_.find(body);auto b=ships_.find(ship);
    return a!=sites_.end() && b!=ships_.end() && move(a->second.stock,b->second,m,kg);
}
bool MaterialCustody::grow(SiteId body,std::int64_t n) {
    auto it=sites_.find(body);
    if(it==sites_.end() || !fits(n,15) || n>std::numeric_limits<std::uint64_t>::max()-it->second.harvest_cycles) return false;
    auto& s=it->second.stock;
    if(s.water<10*n || s.nutrients<5*n || !safe_sum(s.food,12*n) || !safe_sum(s.wastewater,3*n)) return false;
    s.water-=10*n;s.nutrients-=5*n;s.food+=12*n;s.wastewater+=3*n;
    it->second.harvest_cycles+=static_cast<std::uint64_t>(n);
    return true;
}
bool MaterialCustody::consume_ship_food(std::uint64_t ship,std::int64_t n) {
    auto it=ships_.find(ship);
    if(it==ships_.end() || !fits(n,12)) return false;
    auto& s=it->second;
    if(s.food<12*n || !safe_sum(s.organic_waste,8*n) || !safe_sum(s.wastewater,4*n)) return false;
    s.food-=12*n;s.organic_waste+=8*n;s.wastewater+=4*n;
    return true;
}
bool MaterialCustody::compost(SiteId body,std::int64_t n) {
    auto it=sites_.find(body);
    if(it==sites_.end() || !fits(n,8)) return false;
    auto& s=it->second.stock;
    if(s.organic_waste<8*n || !safe_sum(s.compost,6*n) || !safe_sum(s.wastewater,2*n)) return false;
    s.organic_waste-=8*n;s.compost+=6*n;s.wastewater+=2*n;
    return true;
}
} // namespace galaxy::planetary
