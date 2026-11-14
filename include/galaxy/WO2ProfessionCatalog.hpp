#pragma once
// WO2: read-only professions mapped to source-approved INDUSTRY-01 recipes.
// Does not own actual workers, skills, shifts, equipment, materials or receipts.
// A proposed specialist credential is NOT an earned qualification.
#include "galaxy/Industry01Catalog.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace galaxy::workforce02 {
struct Trade {
    std::string capability,profession,skill_family,level;
    std::uint32_t minimum_workers=0,training_units=0;
    std::string training_policy,native_binding;
    bool power_required=false,native_execution=false;
};
struct ServiceRole {
    std::string staff_role,profession,skill_family,level;
    std::uint32_t training_units=0;
};
const std::vector<Trade>& trades();
const std::vector<ServiceRole>& service_roles();
const Trade* find_trade(const std::string& site_capability) noexcept;
const ServiceRole* find_service_role(const std::string& staff_role) noexcept;
void validate_against_industry01();
} // namespace galaxy::workforce02
