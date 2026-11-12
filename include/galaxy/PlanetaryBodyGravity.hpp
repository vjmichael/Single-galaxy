#pragma once
// S6-F5-C v134: game-world AUTHORITATIVE generated bulk mass, derived only
// from the original immutable astronomical body ID, radius and generator.
// These are synthetic game-world physical inputs, NOT observations of real
// astrophysical mass, and never create/consume mining or planetary resources.
#include <cstdint>
#include <optional>
namespace galaxy::planetary::gravity {
inline std::optional<std::uint64_t> mass_exatons(
        std::uint64_t radius_mm,std::uint64_t density_kg_m3) noexcept {
    if(radius_mm<1000000000ULL || radius_mm>10000000000ULL ||
       radius_mm%1000000ULL || density_kg_m3<2000 || density_kg_m3>7500)
        return std::nullopt;
    const auto km=radius_mm/1000000ULL;
    // Rounded 4*pi/3 = 4.188790; divide early to retain uint64 range.
    const auto cube=km*km*km;
    const auto scaled=(cube*density_kg_m3)/1000000ULL;
    const auto result=(scaled*4188790ULL+500000000ULL)/1000000000ULL;
    return result>0 && result<100000000ULL?
        std::optional<std::uint64_t>(result):std::nullopt;
}
inline std::optional<std::int64_t> surface_gravity_mm_s2(
        std::uint64_t radius_mm,std::uint64_t mass_1e18_kg) noexcept {
    if(radius_mm<1000000000ULL || radius_mm>10000000000ULL ||
       radius_mm%1000000ULL || mass_1e18_kg==0 ||
       mass_1e18_kg>=100000000ULL)return std::nullopt;
    const auto radius_km=radius_mm/1000000ULL;
    // G = 6.6743e-11 m^3 kg^-1 s^-2, expressed in fixed point.
    // g(mm/s^2) = 66743 * (mass/1e18 kg) / radius_km^2.
    const auto denominator=radius_km*radius_km;
    const auto g=(66743ULL*mass_1e18_kg+denominator/2)/denominator;
    return g>=1 && g<=50000 ?
        std::optional<std::int64_t>(static_cast<std::int64_t>(g)):
        std::nullopt;
}
} // namespace galaxy::planetary::gravity
