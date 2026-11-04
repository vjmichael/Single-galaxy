#pragma once
// PLAY-13B.4 — reproducible astronomical diversity for a fictional galaxy.
// Game-generation archetypes, not assertions about observed occurrence rates.
#include <cstdint>
#include <vector>
namespace galaxy {
enum class StellarArchitecture {
    Single, WideBinary, Circumbinary, HierarchicalTriple
};
enum class StarMorphology {
    Ordinary, RapidRotatorOblate, CloseBinaryTidal
};
enum class AstronomicalFeature {
    DebrisBelt, CometPopulation, DustArc, MagnetizedEnvironment,
    StellarRemnant, RogueWorld, RareUnexplainedSignal
};
struct AstronomicalProfile {
    StellarArchitecture architecture=StellarArchitecture::Single;
    StarMorphology morphology=StarMorphology::Ordinary;
    std::uint8_t star_count=1;
    std::vector<AstronomicalFeature> features;
    std::uint64_t generator_version=1;
};
inline std::uint64_t astronomy_mix(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    return x^(x>>31);
}
inline AstronomicalProfile generate_astronomical_profile(
    std::uint64_t galaxy_seed,std::uint32_t system_id) {
    // Non-zero, stable system ID. No cross-platform RNG implementation drift.
    const std::uint64_t seed=astronomy_mix(galaxy_seed^
        (std::uint64_t(system_id)*0x9e3779b97f4a7c15ULL));
    AstronomicalProfile p;
    const auto layout=seed%100;
    if(layout<54) {p.architecture=StellarArchitecture::Single;p.star_count=1;}
    else if(layout<76) {p.architecture=StellarArchitecture::WideBinary;p.star_count=2;}
    else if(layout<94) {p.architecture=StellarArchitecture::Circumbinary;p.star_count=2;}
    else {p.architecture=StellarArchitecture::HierarchicalTriple;p.star_count=3;}
    const auto shape=astronomy_mix(seed)%100;
    if(p.star_count>1 && shape<17)p.morphology=StarMorphology::CloseBinaryTidal;
    else if(shape>=17&&shape<26)p.morphology=StarMorphology::RapidRotatorOblate;
    const auto features=astronomy_mix(seed^0x9a78900f77345611ULL);
    if(features&1ULL)p.features.push_back(AstronomicalFeature::DebrisBelt);
    if(features&2ULL)p.features.push_back(AstronomicalFeature::CometPopulation);
    if(features&4ULL)p.features.push_back(AstronomicalFeature::DustArc);
    if((features>>3)%5==0)p.features.push_back(AstronomicalFeature::MagnetizedEnvironment);
    if((features>>8)%11==0)p.features.push_back(AstronomicalFeature::StellarRemnant);
    if((features>>12)%17==0)p.features.push_back(AstronomicalFeature::RogueWorld);
    if((features>>16)%79==0)p.features.push_back(AstronomicalFeature::RareUnexplainedSignal);
    return p;
}
} // namespace galaxy
