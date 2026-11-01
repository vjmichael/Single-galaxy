#include "galaxy/InventoryLogistics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace galaxy::inventory;
#define CHECK(condition) do {if(!(condition))throw std::runtime_error(std::string("B2c1 line ")+std::to_string(__LINE__)+": " #condition);}while(false)
template<class F> static void reject(F work){bool denied=false;try{work();}catch(const std::exception&){denied=true;}CHECK(denied);}
int main(){
 try{
   // A real input quantity must enter the network explicitly. Links create nothing.
   LogisticsNetwork line;
   line.create_node(100,1000);line.create_node(200,25);line.create_node(300,50);
   line.add_route(10,100,200,"good/aluminum",4);
   line.add_route(20,200,300,"good/aluminum",3);
   const auto empty=line.total_stacks();
   CHECK(line.advance(300)==0);
   CHECK(line.total_stacks()==empty);
   line.seed_from_authoritative_inventory(100,"good/aluminum",40);
   const auto initial=line.total_stacks();
   CHECK(initial.at("good/aluminum")==40);
   auto stepwise=line;
   for(int i=0;i<10;++i)stepwise.advance(1);
   CHECK(line.advance(10)==stepwise.route(10).units_moved+stepwise.route(20).units_moved);
   CHECK(line.tick()==310);
   for(int i=100;i<=300;i+=100)CHECK(line.node(i).stacks()==stepwise.node(i).stacks());
   CHECK(line.route(10).units_moved==stepwise.route(10).units_moved);
   CHECK(line.total_stacks()==initial);
   // Full final output produces legitimate backpressure, without consuming more feed.
   line.advance(1000);
   CHECK(line.node(300).amount("good/aluminum")==40);
   CHECK(line.node(100).amount("good/aluminum")==0);
   CHECK(line.total_stacks()==initial);
   const auto before=line.node(300).stacks();
   CHECK(line.advance(1000000000ULL)==0);
   CHECK(line.node(300).stacks()==before);
   // A second material is not silently transported on an aluminum-only line.
   line.seed_from_authoritative_inventory(100,"good/aluminum_panels",100);
   line.advance(50);
   CHECK(line.node(100).amount("good/aluminum_panels")==100);
   CHECK(line.node(300).amount("good/aluminum_panels")==0);
   // No unbuilt, zero-sized, duplicate or cyclic nodes/links.
   reject([&]{line.create_node(0,100);});
   reject([&]{line.create_node(100,100);});
   reject([&]{line.create_node(400,0);});
   reject([&]{line.add_route(21,300,100,"good/aluminum",1);});
   reject([&]{line.add_route(22,100,999,"good/aluminum",1);});
   reject([&]{line.add_route(23,100,200,"invalid_item",1);});
   reject([&]{line.add_route(20,100,200,"good/aluminum",1);});
   reject([&]{line.seed_from_authoritative_inventory(100,"good/aluminum",10000);});
   // No mass-limit/density physics; capacity is only per-item game volume.
   LogisticsNetwork volume;
   volume.create_node(1,5000);volume.create_node(2,20);
   volume.seed_from_authoritative_inventory(1,"good/aluminum_frames",50);
   volume.add_route(2,1,2,"good/aluminum_frames",7);
   CHECK(volume.advance(1)==7);
   CHECK(volume.node(2).used_liters()==7);
   CHECK(volume.advance(5)==13); // destination fills at 20 litres
   CHECK(volume.advance(1000000000ULL)==0); // blocked route fast-forward
   CHECK(volume.node(1).amount("good/aluminum_frames")==30);
   CHECK(volume.node(2).amount("good/aluminum_frames")==20);
   volume.set_route_enabled(2,false);
   CHECK(volume.advance(1000000000ULL)==0);
   volume.set_route_enabled(2,true);
   CHECK(volume.total_stacks().at("good/aluminum_frames")==50);
   // Shared-source priority uses stable route ID, not wall-clock order.
   LogisticsNetwork branch;
   branch.create_node(1,1000);branch.create_node(2,20);branch.create_node(3,20);
   branch.seed_from_authoritative_inventory(1,"good/aluminum",10);
   branch.add_route(100,1,3,"good/aluminum",7);
   branch.add_route(90,1,2,"good/aluminum",7);
   CHECK(branch.advance(1)==10);
   CHECK(branch.node(2).amount("good/aluminum")==7);
   CHECK(branch.node(3).amount("good/aluminum")==3);
   CHECK(branch.advance(10)==0);
   CHECK(branch.total_stacks().at("good/aluminum")==10);
   // Overflow guard: a hostile tick request must never wrap the time counter.
   CHECK(volume.tick()>1000000000ULL);
   reject([&]{volume.advance(std::numeric_limits<std::uint64_t>::max());});
   std::cout<<"ECON-02B2c1 directed multi-container inventory logistics and backpressure PASS\n";
   return 0;
 }catch(const std::exception& e){std::cerr<<"ECON-02B2c1 FAILED: "<<e.what()<<'\n';return 1;}
}
