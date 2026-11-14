#pragma once
// S6-F5-C v135 original-game atmosphere: sealed deterministic celestial
// generation inputs. These are NOT claims about real exoplanet chemistry.
// No biome, visual color, water, flora or player preference implies air.
#include <cstdint>
namespace galaxy::planetary::air {
struct State {
    std::uint32_t pressure_pa=0;
    std::uint16_t oxygen_permille=0;
    bool toxic=false;
};
inline bool valid(State v) noexcept {
    return v.pressure_pa<=500000 && v.oxygen_permille<=1000 &&
           (v.pressure_pa!=0 || v.oxygen_permille==0);
}
inline bool breathable(State v) noexcept {
    if(!valid(v) || v.toxic ||
       v.pressure_pa<70000 || v.pressure_pa>120000)return false;
    const auto oxygen_partial_pa =
        (std::uint64_t(v.pressure_pa)*v.oxygen_permille)/1000ULL;
    return oxygen_partial_pa>=16000 && oxygen_partial_pa<=30000;
}
inline bool sealed_and_supplied(bool wearing,std::uint32_t integrity,
                                std::uint32_t charge,std::int64_t oxygen_kg) noexcept {
    return wearing&&integrity>=60&&charge>0&&oxygen_kg>0;
}
inline bool thermally_protected(bool wearing,std::uint32_t integrity,
                                std::uint32_t charge) noexcept {
    return wearing&&integrity>=50&&charge>0;
}
} // namespace galaxy::planetary::air
