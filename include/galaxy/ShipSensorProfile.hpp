#pragma once
// SHIP-STATUS-07B: original physical hull sensors. Seeing a silhouette is
// neither receiving a trusted hail nor learning its pilot or legal owner.
// All lengths are millimetres. No magic identity-by-entity-ID broadcast.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <string>

namespace galaxy::ship_sensors {
// Detectable signal classes are capabilities of ORIGINAL installed equipment:
// support hardware ID 1=firewall, 2=passive stealth baffle,
// 3=high-resolution counter-ECM sensor array (distinct from slot indices).
// A hardpoint-mounted survey scanner also assists tracking, but never
// authenticates identity. These hardware IDs occupy the two original
// support rack slots, not new or unlimited equipment locations.
inline constexpr std::int64_t kOrdinaryRangeMm=5000;
inline constexpr std::int64_t kStealthOrdinaryRangeMm=1200;
inline constexpr std::int64_t kStealthSurveyRangeMm=3000;
inline constexpr std::int64_t kStealthStrongRangeMm=5000;
inline constexpr std::uint8_t kOpticalClass=1;
inline constexpr std::uint8_t kDetailedClass=2;
inline constexpr std::uint8_t kHighResolutionClass=3;

inline bool stealth_baffle(const Entity& ship) noexcept {
    return ship.fitting.support[0]==2 || ship.fitting.support[1]==2;
}
inline bool strong_sensors(const Entity& ship) noexcept {
    return ship.fitting.support[0]==3 || ship.fitting.support[1]==3;
}
inline bool powered_survey_scanner(
    const Simulation& world,const Entity& observer) {
    for(std::uint8_t slot=0;slot<2;++slot)
        if(observer.fitting.hardpoints[slot]==1 &&
           world.hardpoint_effective_on(observer.id,slot))
            return true;
    return false;
}
inline std::int64_t detection_range_mm(
    const Simulation& world,
    const Entity& observer,const Entity& target) {
    if(observer.type!=EntityType::Ship || target.type!=EntityType::Ship)
        return 0;
    if(!stealth_baffle(target))return kOrdinaryRangeMm;
    if(strong_sensors(observer))return kStealthStrongRangeMm;
    if(powered_survey_scanner(world,observer))
        return kStealthSurveyRangeMm;
    return kStealthOrdinaryRangeMm;
}
// Active intrusion equipment produces electronic interference, but an
// observer with counter-ECM sensors can still acquire the hull silhouette.
// This is a deterministic *classification*, not an account ID.
inline std::uint8_t detail_class(
    const Simulation& world,const Entity& observer,const Entity& target) {
    if(strong_sensors(observer))return kHighResolutionClass;
    if(powered_survey_scanner(world,observer) &&
       !target.fitting.intrusion_emitter())return kDetailedClass;
    return kOpticalClass;
}
inline std::string description_at_detail(const Simulation& world,
    EntityId target_ship,std::uint8_t detail) {
    const auto* t=world.find(target_ship);
    if(!t || t->type!=EntityType::Ship ||
       detail<kOpticalClass || detail>kHighResolutionClass)return {};
    // Exact reproducible hull description at a specific available sensor
    // grade. Matching visually identical ships must not identify owners.
    const auto class_name=world.colony_ship(target_ship)?
        "COLONY_HULL":"OTHER_HULL";
    std::string result=std::string(class_name)+
        "|HULL_"+std::to_string(t->hull)+
        "|CARGO_"+std::to_string(t->cargo_capacity);
    if(detail>=kDetailedClass)
        result+="|HP_"+std::to_string(unsigned(t->fitting.hardpoints[0]))+
            "_"+std::to_string(unsigned(t->fitting.hardpoints[1]))+
            "|SUPPORT_COUNT_"+std::to_string(
                int(t->fitting.support[0]!=0)+int(t->fitting.support[1]!=0));
    if(detail>=kHighResolutionClass)
        result+="|SUPPORT_"+std::to_string(unsigned(t->fitting.support[0]))+
            "_"+std::to_string(unsigned(t->fitting.support[1]))+
            "|SUBSYSTEM_"+std::to_string(unsigned(t->fitting.subsystems[0]))+
            "_"+std::to_string(unsigned(t->fitting.subsystems[1]))+
            "|RIG_"+std::to_string(unsigned(t->fitting.rigging[0]));
    // Manufacturing receipt IDs are private provenance and must NOT leak
    // through a high-resolution optical sensor as a magic registration lookup.
    return result;
}
inline std::string description(const Simulation& world,
    EntityId observer_ship,EntityId target_ship) {
    const auto* observer=world.find(observer_ship);
    const auto* target=world.find(target_ship);
    if(!observer || !target)return {};
    return description_at_detail(world,target_ship,
        detail_class(world,*observer,*target));
}
} // namespace galaxy::ship_sensors
