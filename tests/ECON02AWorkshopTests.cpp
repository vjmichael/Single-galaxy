#include "galaxy/EconomyPrototype.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
using namespace galaxy::economy;
static void check(bool ok,const char *why){ if(!ok)throw std::runtime_error(why); }
static void refuses(const std::function<void()> &f){ bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"unsafe action accepted"); }
static void run(Workshop &w,RecipeId r,std::uint64_t worker){const auto id=w.post(r);w.assign(id,worker);w.advance(recipe(r).powered_ticks);check(w.order(id).status==OrderStatus::Completed,"job not done");}
int main(){
 try{
    Workshop w;
    Worker volunteer;volunteer.id=1;
    Worker specialist;specialist.id=2;specialist.proficiency[static_cast<std::size_t>(Skill::Metalwork)]=2;
    specialist.proficiency[static_cast<std::size_t>(Skill::Welding)]=2;
    Worker mechanic;mechanic.id=3;mechanic.proficiency[static_cast<std::size_t>(Skill::Metalwork)]=2;
    mechanic.proficiency[static_cast<std::size_t>(Skill::Mechanical)]=2;
    w.add_worker(volunteer);w.add_worker(specialist);w.add_worker(mechanic);
    refuses([&]{w.add_worker(volunteer);});
    refuses([&]{w.seed_stock(Item::AluminumFeedstock,0);});
    w.seed_stock(Item::AluminumFeedstock,12);
    const auto original=w.total_stock_mass_kg();
    check(original==12,"incorrect starting mass");
    auto auto_request=w.propose_shortage(RecipeId::SmeltIngot);
    refuses([&]{w.assign(auto_request,1);}); // needs only propose, does not authorize action
    w.approve(auto_request);
    w.assign(auto_request,1); // refinery is a critical, zero-proficiency job
    check(w.stock(Item::AluminumFeedstock)==10,"inputs not reserved");
    refuses([&]{w.assign(auto_request,2);});
    auto other=w.post(RecipeId::SmeltIngot);
    refuses([&]{w.assign(other,2);}); // one workstation, one active job
    w.set_powered(false);w.advance(5);
    check(w.receipts().empty(),"brownout fabricated output");
    w.set_powered(true);w.set_worker_presence(1,false);w.advance(5);
    check(w.receipts().empty(),"offsite worker fabricated output");
    w.set_worker_presence(1,true);w.advance(2);
    check(w.receipts().size()==1,"no completion receipt");
    refuses([&]{w.cancel(auto_request);});
    w.assign(other,1);w.cancel(other);
    check(w.stock(Item::AluminumFeedstock)==10,"cancellation did not refund");
    check(w.busy_stations()==0,"cancel left station busy");
    // The same workshop processes many goods, unlike resource-tier-locked machines.
    run(w,RecipeId::SmeltIngot,1);
    run(w,RecipeId::SmeltIngot,1);
    check(w.stock(Item::AluminumIngot)==6,"ingot production wrong");
    for(int i=0;i<5;++i)run(w,RecipeId::RollSheet,1);
    run(w,RecipeId::DrawWire,1);
    check(w.stock(Item::AluminumSheet)==5 && w.stock(Item::AluminumWire)==1,"intermediate goods incorrect");
    run(w,RecipeId::WeldBracket,1);
    run(w,RecipeId::FitValve,1);
    // One role alone is insufficient to assemble a specialist item.
    const auto advanced=w.post(RecipeId::AssembleHullPanel);
    refuses([&]{w.assign(advanced,1);});
    refuses([&]{w.assign(advanced,3);});
    w.assign(advanced,2);w.advance(3);
    check(w.stock(Item::HullPanel)==1,"specialist hull panel not manufactured");
    const auto pump=w.post(RecipeId::AssemblePump);
    refuses([&]{w.assign(pump,2);});
    w.assign(pump,3);w.advance(3);
    check(w.stock(Item::PumpAssembly)==1,"dual-skill pump not manufactured");
    check(w.total_stock_mass_kg()==original,"mass changed across multi-stage value chain");
    check(w.receipts().size()==13,"unexpected completed work count");
    check(w.worker(1).proficiency[static_cast<std::size_t>(Skill::Refining)]>=3,"basic productive work did not build skill");
    const auto never=w.propose_shortage(RecipeId::AssemblePump);
    w.cancel(never);refuses([&]{w.approve(never);});
    w.set_board_stage(BoardStage::LaborDivision);
    check(w.board_stage()==BoardStage::LaborDivision,"organizational board stage missing");
    check(w.receipts().back().worker_id==3,"source worker not identified");
    std::cout<<"ECON02A: workshop recipe graph, inclusive jobs, dual skills, single-worker scheduling, power/absence, proposal approval, mass and receipts: PASS\n";
 }catch(const std::exception &ex){std::cerr<<"ECON02A FAILED: "<<ex.what()<<'\n';return 1;}
}
