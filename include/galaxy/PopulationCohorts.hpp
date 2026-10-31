#pragma once
#include <cstdint>
#include <map>

namespace galaxy {
// B7.0n-S: stable population strata. One record represents COUNT residents,
// never one fictional person with an average personality. Explicit dimensions
// preserve distribution information relevant to the current calculation.
struct CohortKey {
    std::uint64_t settlement=0;
    std::uint8_t occupation=0;      // occupation category, not a fictional individual
    std::uint8_t skill_band=0;      // 0..4, retained as distribution
    std::uint8_t expectation_band=0;// 0..4, influences later satisfaction rules
    bool operator<(const CohortKey& b) const noexcept {
        if(settlement!=b.settlement)return settlement<b.settlement;
        if(occupation!=b.occupation)return occupation<b.occupation;
        if(skill_band!=b.skill_band)return skill_band<b.skill_band;
        return expectation_band<b.expectation_band;
    }
};
class PopulationCohorts {
public:
    void add(CohortKey key,std::uint64_t count);
    void transfer(CohortKey from,CohortKey to,std::uint64_t count);
    std::uint64_t count(CohortKey key) const noexcept;
    std::uint64_t population() const noexcept;
    std::size_t cohort_count() const noexcept {return groups_.size();}
    // Fixed-point expected share of the SOURCE stratum, capped by transport.
    // No per-NPC loop. Keep all inputs and decisions observable to the caller.
    std::uint64_t migrate_fraction(CohortKey from,CohortKey to,
                                   std::uint32_t permille,std::uint64_t transport_capacity);
private:
    static void validate(CohortKey key);
    std::map<CohortKey,std::uint64_t> groups_;
    std::uint64_t population_=0;
};
} // namespace galaxy
