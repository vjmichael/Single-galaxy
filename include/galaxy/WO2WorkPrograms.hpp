#pragma once
// WO2: derive bounded resource-backed jobs from the INDUSTRY-01 acyclic recipe
// graph. This is a material/skill PLANNING view, not a world inventory,
// work order, NPC skill record, energy reservation or executable production.
// Only original, owner-authorized Simulation commands can do real work.
#include "galaxy/WO2ProfessionCatalog.hpp"
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace galaxy::workforce02 {
struct ProductionStep {
    std::string recipe_id,site_capability,profession,skill_family;
    std::uint64_t batches=0,total_work_ticks=0,total_energy_kj=0;
    std::uint32_t minimum_workers=0;
    bool currently_native_executable=false;
};
struct WorkProgram {
    std::string goal_item;
    std::uint64_t goal_quantity=0;
    std::vector<ProductionStep> steps; // inputs precede products, not dispatch order
    std::map<std::string,std::uint64_t> required_source_items;
    std::uint64_t sum_worker_ticks=0,total_energy_kj=0;
    bool all_processes_native=false;
};
struct ServiceProgram {
    std::string service_id;
    std::vector<std::string> required_professions;
    std::vector<std::string> site_capabilities;
    std::vector<std::string> consumables;
    std::vector<std::string> durable_equipment;
    bool executable_now=false;
};
inline std::uint64_t checked_add(std::uint64_t a,std::uint64_t b) {
    if(b>std::numeric_limits<std::uint64_t>::max()-a)
        throw std::overflow_error("WO2 planning integer overflow");
    return a+b;
}
inline std::uint64_t checked_mul(std::uint64_t a,std::uint64_t b) {
    if(b && a>std::numeric_limits<std::uint64_t>::max()/b)
        throw std::overflow_error("WO2 planning integer overflow");
    return a*b;
}
inline WorkProgram plan_material_program(const std::string& item,
                                         std::uint64_t quantity) {
    // A bounded design projection: raw source goods must be discovered,
    // extracted, purchased? NO: the current owner's resource-only design
    // requires actual mining/farming/transport in later work orders.
    if(quantity==0 || quantity>1000000000000ULL ||
       !industry01::find_item(item))
        throw std::invalid_argument("WO2 unknown or unbounded demand");
    WorkProgram plan{};
    plan.goal_item=item;plan.goal_quantity=quantity;
    plan.all_processes_native=true;
    std::set<std::string> stack;
    const auto descend=[&](auto&& self,const std::string& code,
                           std::uint64_t units,unsigned depth)->void {
        if(depth>40 || plan.steps.size()>512 ||
           !stack.insert(code).second)
            throw std::invalid_argument("WO2 cyclic or oversized work graph");
        const industry01::Recipe* recipe=nullptr;
        // Choose a primary finished-good recipe only. Recycling is an
        // intentional loss-bounded alternate, never the default free source.
        for(const auto& r:industry01::recipes()) {
            if(!r.outputs.empty() && r.outputs[0].code==code) {
                recipe=&r;break;
            }
        }
        if(!recipe) {
            auto& old=plan.required_source_items[code];
            old=checked_add(old,units);
            plan.all_processes_native=false;
            stack.erase(code);
            return;
        }
        const auto* trade=find_trade(recipe->site_capability);
        if(!trade)
            throw std::invalid_argument("WO2 industry recipe lacks a real trade");
        const auto per_batch=recipe->outputs.front().quantity;
        if(!per_batch)
            throw std::invalid_argument("WO2 zero-output recipe");
        const auto batches=1+(units-1)/per_batch; // safe ceil, no overflow
        for(const auto& ingredient:recipe->inputs) {
            const auto needed=checked_mul(batches,ingredient.quantity);
            self(self,ingredient.code,needed,depth+1);
        }
        ProductionStep step{};
        step.recipe_id=recipe->code;
        step.site_capability=recipe->site_capability;
        step.profession=trade->profession;
        step.skill_family=trade->skill_family;
        step.batches=batches;
        step.total_work_ticks=checked_mul(batches,recipe->work_ticks);
        step.total_energy_kj=checked_mul(batches,recipe->energy_kj);
        step.minimum_workers=trade->minimum_workers;
        step.currently_native_executable=trade->native_execution &&
            industry01::is_native_workshop_recipe(recipe->code);
        if(!step.currently_native_executable)plan.all_processes_native=false;
        plan.sum_worker_ticks=checked_add(plan.sum_worker_ticks,
            checked_mul(step.total_work_ticks,step.minimum_workers));
        plan.total_energy_kj=checked_add(plan.total_energy_kj,step.total_energy_kj);
        plan.steps.push_back(std::move(step));
        stack.erase(code);
    };
    descend(descend,item,quantity,0);
    return plan;
}
inline ServiceProgram describe_service_program(const std::string& id) {
    const auto* service=industry01::find_service(id);
    if(!service)throw std::invalid_argument("WO2 unknown service");
    ServiceProgram plan{};
    plan.service_id=id;
    plan.site_capabilities=service->site_capabilities;
    plan.consumables=service->consumables;
    plan.durable_equipment=service->equipment;
    for(const auto& staff:service->staff){
        const auto* profile=find_service_role(staff);
        if(!profile)throw std::invalid_argument("WO2 undefined service job");
        plan.required_professions.push_back(profile->profession);
    }
    // No specialist service is authorized until a real built facility,
    // named worker certification and original-world action exists.
    plan.executable_now=false;
    return plan;
}
} // namespace galaxy::workforce02
