#include "galaxy/PopulationCohorts.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace galaxy {
void PopulationCohorts::validate(CohortKey key){
    if(!key.settlement || key.skill_band>4 || key.expectation_band>4)
        throw std::invalid_argument("invalid population cohort category");
}
void PopulationCohorts::add(CohortKey key,std::uint64_t n){
    validate(key);
    if(n>std::numeric_limits<std::uint64_t>::max()-population_ ||
       n>std::numeric_limits<std::uint64_t>::max()-count(key))
        throw std::overflow_error("cohort population overflow");
    if(!n)return;
    groups_[key]+=n;
    population_+=n;
}
std::uint64_t PopulationCohorts::count(CohortKey key) const noexcept {
    const auto it=groups_.find(key);return it==groups_.end()?0:it->second;
}
std::uint64_t PopulationCohorts::population() const noexcept {return population_;}
void PopulationCohorts::transfer(CohortKey from,CohortKey to,std::uint64_t n){
    validate(from);validate(to);
    if(n>count(from))throw std::invalid_argument("transfer exceeds source cohort");
    if(from.settlement==to.settlement && from.occupation==to.occupation &&
       from.skill_band==to.skill_band && from.expectation_band==to.expectation_band)return;
    if(n>std::numeric_limits<std::uint64_t>::max()-count(to))
        throw std::overflow_error("destination cohort overflow");
    if(!n)return;
    auto it=groups_.find(from);
    it->second-=n;
    if(!it->second)groups_.erase(it);
    groups_[to]+=n;
}
std::uint64_t PopulationCohorts::migrate_fraction(CohortKey from,CohortKey to,
                                       std::uint32_t rate,std::uint64_t capacity){
    if(rate>1000)throw std::invalid_argument("invalid migration rate");
    validate(from);validate(to);
    const auto n=count(from);
    // Exact fixed-point integer arithmetic; no overflow from n*rate.
    const auto moving=(n/1000)*rate+((n%1000)*rate)/1000;
    const auto qty=std::min(moving,capacity);
    transfer(from,to,qty);return qty;
}
} // namespace galaxy
