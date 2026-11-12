#include "galaxy/Inventory.hpp"
#include <limits>
#include <stdexcept>
#include <utility>

namespace galaxy::inventory {
Catalogue::Catalogue(std::vector<ItemDefinition> definitions) {
    if(definitions.empty())throw std::invalid_argument("empty item catalogue");
    for(auto& d:definitions) {
        if(d.code.empty() || d.code.find_first_of("|\r\n")!=std::string::npos ||
           d.liters_per_unit<=0 || d.liters_per_unit>1000000000)
            throw std::invalid_argument("invalid item definition");
        if(!volumes_.emplace(std::move(d.code),d.liters_per_unit).second)
            throw std::invalid_argument("duplicate item code");
    }
}
const Catalogue& Catalogue::standard() {
    // All values are deliberate gameplay balance units, NOT physical density.
    // Exact code names are versioned game data, never arbitrary client strings.
    static const Catalogue c({
        {"ore",1000},{"refined",1000},{"construction_module",2000},
        {"tailings",1},
        {"mineral/serpentine",1},{"mineral/saponite",1},
        {"mineral/magnetite",1},{"mineral/pyrrhotite",1},
        {"mineral/carbonates",1},{"mineral/olivine",1},
        {"mineral/pyroxene",1},{"mineral/nickel_iron",1},
        {"mineral/other",1},{"mineral/plagioclase",1},
        {"mineral/troilite",1},
        {"good/structural_alloy",1},{"good/aluminum",1},
        {"good/aluminum_panels",1},{"good/aluminum_frames",1},
        {"good/aluminum_scrap",1},{"good/ceramic",1},
        {"good/glass",1},{"good/water",1},{"good/sulfur",1},
        {"good/carbon_feed",1},
        // Sealed transport holds are real ship cargo, not life-support supply.
        {"good/freight_water",1},{"good/freight_food",1},
        {"good/freight_nutrients",1},
        // S6-D7: identifiable, source-funded physical planetary freight.
        {"good/planetary_soil",1},{"good/planetary_mineral",1},
        {"good/planetary_volatile",1}
    });
    return c;
}
bool Catalogue::contains(const std::string& code) const noexcept {
    return volumes_.find(code)!=volumes_.end();
}
std::int64_t Catalogue::liters_per_unit(const std::string& code) const {
    auto i=volumes_.find(code);
    if(i==volumes_.end())throw std::invalid_argument("unknown item: "+code);
    return i->second;
}
StackStore::StackStore(const Catalogue& catalogue,std::int64_t capacity_liters)
 : catalogue_(&catalogue),capacity_liters_(capacity_liters) {
    if(capacity_liters<0)throw std::invalid_argument("invalid store capacity");
}
void StackStore::load(const std::string& code,std::int64_t amount) {
    if(amount<0 || !catalogue_->contains(code))
        throw std::invalid_argument("invalid initial stack");
    if(amount==0)return;
    const auto volume=catalogue_->liters_per_unit(code);
    if(amount>free_liters()/volume ||
       amount>std::numeric_limits<std::int64_t>::max()-this->amount(code))
        throw std::overflow_error("store capacity exceeded");
    stacks_[code]+=amount;
    used_liters_+=amount*volume;
}
std::int64_t StackStore::amount(const std::string& code) const noexcept {
    const auto i=stacks_.find(code);
    return i==stacks_.end()?0:i->second;
}
TransferStatus StackStore::transfer_to(StackStore& destination,
                                       const std::string& code,std::int64_t qty) {
    if(qty<=0 || this==&destination)return TransferStatus::InvalidAmount;
    if(catalogue_!=destination.catalogue_)return TransferStatus::IncompatibleCatalogue;
    if(!catalogue_->contains(code))return TransferStatus::InvalidItem;
    if(amount(code)<qty)return TransferStatus::SourceShortage;
    const auto volume=catalogue_->liters_per_unit(code);
    if(qty>destination.free_liters()/volume ||
       qty>std::numeric_limits<std::int64_t>::max()-destination.amount(code))
        return TransferStatus::DestinationFull;
    // All checks have passed. Materialize the destination map slot first:
    // allocation failure cannot withdraw an item from the source.
    auto& dest_amount=destination.stacks_[code];
    dest_amount+=qty;
    auto& source=stacks_.at(code);
    source-=qty;
    if(!source)stacks_.erase(code);
    const auto occupied=qty*volume;
    used_liters_-=occupied;
    destination.used_liters_+=occupied;
    return TransferStatus::Applied;
}
} // namespace galaxy::inventory
