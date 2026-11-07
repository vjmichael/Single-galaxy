#pragma once
// INDUSTRY-01. Read-only source-generated recipe / item descriptions.
// No duplicate physical inventory or worker authority. Original
// Simulation / PersistentWorld / QuantifiedLine remains sole world writer.
#include "galaxy/QuantifiedProduction.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace galaxy::industry01 {
struct Item {
    std::string code,group,unit,lifecycle;
    std::uint64_t mass_g_per_unit=0,cargo_mm3_per_unit=0;
    std::vector<std::string> need_tags;
};
struct Ingredient {std::string code;std::uint64_t quantity=0;};
struct Recipe {
    std::string code,site_capability;
    std::uint64_t work_ticks=0,energy_kj=0;
    std::vector<Ingredient> inputs,outputs;
    std::string native_binding;
};
struct Service {
    std::string code,result;
    std::vector<std::string> staff,site_capabilities,consumables,equipment;
};
struct LegacyUnit {
    std::string code,kind,source_field;
    std::uint64_t storage_liters_per_unit=0,mass_g_per_unit=0;
};
const std::vector<Item>& items();
const std::vector<Recipe>& recipes();
const std::vector<Service>& services();
const std::vector<LegacyUnit>& legacy_units();
const Item* find_item(const std::string& code) noexcept;
const Recipe* find_recipe(const std::string& code) noexcept;
const Service* find_service(const std::string& code) noexcept;
// The only currently native-executable INDUSTRY-01 bindings are actual
// existing site workshops; the other catalogue entries are bounded
// research/data definitions and must fail closed until implemented.
bool is_native_workshop_recipe(const std::string& code) noexcept;
industry::Recipe original_workshop_recipe(const std::string& code);
// Checks source-generated recipes, original catalogue units and
// source-of-truth workshop bindings without touching a saved world.
void validate_against_original_inventory();
} // namespace galaxy::industry01
