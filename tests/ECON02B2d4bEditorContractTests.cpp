// The browser's exported .sglayout is parsed by the exact authoritative C++
// validator and compiled to the existing durable construction job contract.
#include "galaxy/FactoryLayout.hpp"
#include "galaxy/BlueprintConstruction.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::industry;
#ifndef SG_EDITOR_FIXTURE_PATH
#error Fixture path not provided by CMake
#endif
int main(){
 try{
    std::ifstream in(SG_EDITOR_FIXTURE_PATH);
    if(!in)throw std::runtime_error("missing editor export fixture");
    const std::string text((std::istreambuf_iterator<char>(in)),{});
    const auto blueprint=FactoryLayout::from_blueprint(text);
    if(blueprint.canonical_blueprint()!=text)throw std::runtime_error("editor output not canonical");
    if(blueprint.devices().size()!=3 || blueprint.conveyors().size()!=2 ||
       blueprint.devices().at(kWorkshopConveyorNode).width!=2)
        throw std::runtime_error("editor output has wrong device geometry");
    const auto plan=BlueprintConstruction(blueprint,123,456,7,10000);
    if(plan.jobs().size()!=16)throw std::runtime_error("editor plan must compile to 16 world jobs");
    unsigned cost=0;for(const auto& job:plan.jobs())cost+=job.module_cost;
    if(cost!=11)throw std::runtime_error("editor module cost mismatches authoritative planner");
    if(BlueprintConstruction::from_manifest(plan.manifest()).manifest()!=plan.manifest())
        throw std::runtime_error("compiled plan cannot be restored");
    std::cout<<"ECON-02B2d4b browser canonical export validated by native construction contract PASS\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<"ECON-02B2d4b FAIL: "<<e.what()<<'\n';return 1;}
}
