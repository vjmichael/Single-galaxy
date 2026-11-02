// ECON-03c9 — 8 million people, paired and all-to-all cold production/freight.
// This DOES exercise native finite-stock industry, authenticated host policy,
// SQLite receipts, and the regional mode authority. Visual warm mode remains a
// read-only synthetic projection, NOT a detailed 1-million-NPC PersistentWorld.
#include "galaxy/RegionalColdIndustry.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
using namespace galaxy::regional_events;
using namespace galaxy::cold_industry;
namespace fs=std::filesystem;
static constexpr std::uint64_t TPH=18000, STEP=4500, END=8*TPH;
static constexpr std::uint64_t PEOPLE=1'000'000;
static const std::array<std::string,8> names={"A1","A2","B1","B2","C1","C2","D1","D2"};
static const std::array<std::string,4> scopes={"A","B","C","D"};
static const std::array<int,7> itinerary={0,1,2,3,2,1,0};
static const std::string actor="cold-economy-host-owned-test";
static void expect(bool x,const std::string& why){if(!x)throw std::runtime_error(why);}
static void exec(sqlite3* db,const std::string& cmd){
    char* error=nullptr;
    if(sqlite3_exec(db,cmd.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string msg=error?error:sqlite3_errmsg(db);sqlite3_free(error);
        throw std::runtime_error("SQLite scenario: "+msg);
    }
}
static std::int64_t scalar(sqlite3* db,const std::string& cmd){
    sqlite3_stmt* q=nullptr;
    expect(sqlite3_prepare_v2(db,cmd.c_str(),-1,&q,nullptr)==SQLITE_OK,"scalar prepare");
    const int rc=sqlite3_step(q);
    expect(rc==SQLITE_ROW,"scalar query failed");
    const auto v=sqlite3_column_int64(q,0);sqlite3_finalize(q);return v;
}
static std::int64_t life(const ColonyLifeSupport& c){
    return c.food_kg+c.water_kg+c.oxygen_kg+c.nutrients_kg+c.co2_kg+c.crops_kg+
        c.compost_kg+c.wastewater_kg+c.organic_waste_kg+c.sludge_kg+c.hydrogen_kg;
}
struct Region {
    std::string id,db,a,b;
    std::unique_ptr<CheckpointStore> store;
    std::unique_ptr<RegionalModeAuthority> authority;
    std::unique_ptr<Economy> economy;
    Region(const std::string& name,const std::string& sqlite_path,bool initialize)
        :id(name),db(sqlite_path),a(name+"1"),b(name+"2"){
        open(initialize);
    }
    void open(bool initialize){
        store=std::make_unique<CheckpointStore>(db,id);
        if(initialize){
            Scheduler s(9000); // 1 million consumers every half-game-hour
            cold_colony::State p;p.population=PEOPLE;
            p.habitat.food_kg=6'000'000;
            p.habitat.water_kg=6'000'000;
            p.habitat.oxygen_kg=6'000'000;
            p.depot.food_kg=18'000'000;
            p.depot.water_kg=18'000'000;
            p.depot.oxygen_kg=18'000'000;
            p.depot.nutrients_kg=20'000'000;
            s.add_colony(a,p);s.add_colony(b,p);
            expect(store->save(s,0)==1,"initialize regional scheduler");
        }
        authority=std::make_unique<RegionalModeAuthority>(*store);
        if(initialize)expect(authority->enroll().owner==SimulationOwner::Cold,"initial cold owner");
        economy=std::make_unique<Economy>(*store,db,[](const std::string& who,const std::string& site){
            return who==actor&&!site.empty();
        });
        if(initialize){
            const std::int64_t x=(id[0]-'A')*4'000'000;
            for(const auto& colony:{a,b}){
                SiteSeed seed;seed.colony=colony;
                seed.x_m=x+(colony.back()=='2'?100'000:0);
                seed.deposit_ore_kg=100'000'000;
                seed.solar_kwh_per_hour=600'000;
                economy->seed(seed);
                AutoPlan plan;plan.mine_kg=100'000;plan.harvest_food_kg=500'000;
                plan.recycle_organic_kg=400'000;plan.reclaim_water_kg=400'000;
                plan.reclaim_oxygen_kg=400'000;plan.smelt_ore_kg=80'000;
                plan.fabricate_metal_kg=60'000;
                plan.habitat_supply={400'000,400'000,400'000};
                economy->set_initial_plan(colony,plan);
                for(int k=0;k<12;++k){
                    CarrierSeed ship;ship.id="ship-"+colony+"-"+std::to_string(k);
                    ship.home=colony;ship.capacity_kg=900'000;ship.speed_m_per_hour=10'000'000;
                    ship.fuel_units=50'000;ship.crew=4;
                    economy->seed_carrier(ship);
                }
            }
        }
    }
    void reopen(){economy.reset();authority.reset();store.reset();open(false);}
    VersionedCheckpoint cp()const{auto c=store->load();expect(bool(c),"checkpoint missing");return *c;}
    RegionalAuthorityState mode()const{return authority->state();}
};
struct ScenarioRun {
    std::string db,dir;
    bool all_to_all=false;
    std::array<std::unique_ptr<Region>,4> regions;
    std::ofstream csv;
    std::uint64_t work_batches=0,shipments=0,arrivals=0,handoffs=0,stale_rejections=0,restarts=0;
    std::uint64_t maximum_inflight=0,failed_commands=0;
    std::array<unsigned,8> carrier_counter{};
    std::int64_t baseline_mass=0;
    std::vector<std::string> visits;
    sqlite3* audit=nullptr;
    ScenarioRun(std::string folder,bool every):dir(std::move(folder)),all_to_all(every){
        fs::create_directories(dir);db=dir+"/world.sqlite";
        if(fs::exists(db))fs::remove(db);
        for(int i=0;i<4;++i)regions[i]=std::make_unique<Region>(scopes[i],db,true);
        expect(sqlite3_open(db.c_str(),&audit)==SQLITE_OK,"open scenario audit");
        sqlite3_busy_timeout(audit,3000);
        exec(audit,"CREATE TABLE IF NOT EXISTS sg_scenario_handoff(scope TEXT NOT NULL,"
                   "direction TEXT NOT NULL,tick INTEGER NOT NULL,checkpoint_revision INTEGER NOT NULL)");
        csv.open(dir+"/hourly_snapshot.csv");
        csv<<"game_hour,colony,population,food_habitat,water_habitat,oxygen_habitat,nutrients_depot,"
              "food_depot,ore_deposit,raw_ore,metal,parts,unserved_cycles,pending_shipments,total_mass\n";
        baseline_mass=mass();
    }
    ~ScenarioRun(){if(audit)sqlite3_close(audit);}
    Region& where(const std::string& colony){
        const auto i=static_cast<int>(colony[0]-'A');
        expect(i>=0&&i<4,"invalid colony prefix");return *regions[i];
    }
    static std::string ship_id(const std::string& colony,int slot){
        return "ship-"+colony+"-"+std::to_string(slot);
    }
    void process_arrivals(std::uint64_t to_tick){
        const auto pending=regions[0]->economy->freight();
        std::vector<Freight> due;
        for(const auto& item:pending)
            if(item.stage==0&&item.arrives<=to_tick)due.push_back(item);
        std::stable_sort(due.begin(),due.end(),[](const Freight& a,const Freight& b){
            return a.arrives<b.arrives || (a.arrives==b.arrives&&a.id<b.id);
        });
        for(const auto& f:due){
            auto& r=where(f.destination);const auto cp=r.cp();
            const auto s=r.mode();expect(s.owner==SimulationOwner::Cold,"arrival into unowned cold region");
            if(arrivals==0){
                const auto before=mass();
                exec(audit,"CREATE TRIGGER sg_fault_arrival BEFORE UPDATE ON sg_cold_freight "
                           "WHEN NEW.stage=1 BEGIN SELECT RAISE(ABORT,'injected arrival fault'); END");
                try{
                    r.economy->deliver("fault-arrive-"+f.id,actor,f.id,cp.revision,s.generation);
                    throw std::runtime_error("injected arrival failure was accepted");
                }catch(const std::runtime_error& e){
                    if(std::string(e.what())=="injected arrival failure was accepted")throw;
                    failed_commands++;
                }
                exec(audit,"DROP TRIGGER sg_fault_arrival");
                expect(r.cp().revision==cp.revision&&mass()==before,
                       "failed arrival changed checkpoint or physical custody");
            }
            auto receipt=r.economy->deliver("deliver-"+f.id,actor,f.id,cp.revision,s.generation);
            expect(!receipt.replayed,"unexpected duplicate arrival");
            arrivals++;
            // Exact replay cannot credit a second time, even after subsequent commands.
            if(arrivals==1){
                const auto again=r.economy->deliver("deliver-"+f.id,actor,f.id,r.cp().revision,s.generation);
                expect(again.replayed,"delivery retry did not deduplicate");
            }
        }
    }
    void run_work(std::uint64_t t){
        for(auto& r:regions){
            const auto count=r->economy->advance_autonomous(actor,t,r->mode().generation);
            expect(count==2,"two native NPC work policy shifts expected per region");
            work_batches+=count;
        }
        // Failed authorization must not run any additional work or mutate time.
        if(t==STEP){
            auto& r=*regions[0];const auto rev=r.cp().revision;
            try{r.economy->advance_autonomous("untrusted-player",t+STEP,r.mode().generation);
                throw std::runtime_error("untrusted autonomous operation accepted");}
            catch(const std::runtime_error& e){
                if(std::string(e.what())=="untrusted autonomous operation accepted")throw;
                failed_commands++;
            }
            expect(r.cp().revision==rev,"unauthorized work changed checkpoint");
        }
    }
    void dispatch_one(const std::string& from,const std::string& to,
                      std::uint64_t t,const std::string& suffix){
        auto& r=where(from);
        const auto idx=static_cast<std::size_t>(std::find(names.begin(),names.end(),from)-names.begin());
        expect(idx<8,"bad source colony");
        const auto carrier=ship_id(from,static_cast<int>(carrier_counter[idx]++%12));
        const auto unique=from+"-"+to+"-"+std::to_string(t)+"-"+suffix;
        const Goods cargo{50'000,30'000,30'000,10'000,5'000,5'000};
        const auto cp=r.cp();
        auto receipt=r.economy->dispatch("dispatch-"+unique,actor,unique,carrier,from,to,cargo,
                                         t,cp.revision,r.mode().generation);
        expect(!receipt.replayed,"unexpected repeated physical dispatch");shipments++;
        if(shipments==1){
            const auto mass_before=mass();const auto rev_before=r.cp().revision;
            exec(audit,"CREATE TRIGGER sg_fault_dispatch BEFORE INSERT ON sg_cold_freight "
                       "BEGIN SELECT RAISE(ABORT,'injected dispatch fault'); END");
            try{
                const auto unused_ship=ship_id(from,11);
                r.economy->dispatch("fault-dispatch",actor,"fault-dispatch",unused_ship,from,to,cargo,
                                    t,rev_before,r.mode().generation);
                throw std::runtime_error("faulted carrier reservation succeeded");
            }catch(const std::runtime_error& e){
                if(std::string(e.what())=="faulted carrier reservation succeeded")throw;
                failed_commands++;
            }
            exec(audit,"DROP TRIGGER sg_fault_dispatch");
            expect(r.cp().revision==rev_before&&mass()==mass_before,
                   "faulted dispatch did not atomically roll back source inventory");
            // Physical ship is busy until after the explicitly modeled return leg.
            try{
                const auto testcp=r.cp();
                r.economy->dispatch("overlap-"+unique,actor,"overlap-"+unique,carrier,from,to,cargo,
                                    t,testcp.revision,r.mode().generation);
                throw std::runtime_error("overlapping carrier mission accepted");
            }catch(const std::runtime_error& e){
                if(std::string(e.what())=="overlapping carrier mission accepted")throw;
                failed_commands++;
            }
        }
    }
    void dispatch_hour(std::uint64_t hour,std::uint64_t t){
        for(int i=0;i<8;++i){
            const auto to=i%2==0?i+1:i-1;
            dispatch_one(names[i],names[to],t,"local");
        }
        if(all_to_all){
            for(int i=0;i<8;++i){
                const auto to=(i+static_cast<int>(hour))%8;
                expect(to!=i,"all-to-all self-route");
                dispatch_one(names[i],names[to],t,"network");
            }
        }
    }
    std::uint64_t in_flight()const{
        std::uint64_t n=0;
        for(const auto& f:regions[0]->economy->freight())if(f.stage==0)++n;
        return n;
    }
    void stage(sqlite3* dbh,const Region& r,const std::string& dir,
               const Scheduler& scheduler){
        sqlite3_stmt* q=nullptr;
        const char* source="INSERT INTO sg_scenario_handoff(scope,direction,tick,checkpoint_revision)"
                           " VALUES (?1,?2,?3,?4)";
        expect(sqlite3_prepare_v2(dbh,source,-1,&q,nullptr)==SQLITE_OK,"handoff stage prepare");
        sqlite3_bind_text(q,1,r.id.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(q,2,dir.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_int64(q,3,static_cast<sqlite3_int64>(scheduler.tick()));
        sqlite3_bind_int64(q,4,static_cast<sqlite3_int64>(r.cp().revision));
        const auto rc=sqlite3_step(q);sqlite3_finalize(q);
        expect(rc==SQLITE_DONE,"handoff stage SQLite write");
    }
    void visit(int i,std::uint64_t t){
        auto& region=*regions[i];
        const auto cp=region.cp();const auto st=region.mode();
        expect(st.owner==SimulationOwner::Cold&&cp.scheduler.tick()==t,
               "camera switch requires current region checkpoint");
        const auto total_before=mass();const auto ships_before=in_flight();
        const auto promote_id="view-"+region.id+"-"+std::to_string(t);
        region.authority->prepare(promote_id,SimulationOwner::Detailed,
                                  st.generation,cp.revision,t);
        const auto promoted=region.authority->commit(promote_id,st.generation,cp.revision,
            [&](sqlite3* dbh,Scheduler& candidate){
                expect(candidate.tick()==t,"cold projection changed tick");
                stage(dbh,region,"promote",candidate);
            });
        expect(promoted.owner==SimulationOwner::Detailed,"promotion failed");
        // The stale cold-worker fence must hold during the ownership transfer.
        Work stale;stale.command_id="stale-"+promote_id;stale.actor=actor;
        stale.colony=region.a;stale.tick=t+STEP;
        try{region.economy->work(stale,cp.revision,st.generation);
            throw std::runtime_error("stale cold executor mutated detailed region");}
        catch(const std::runtime_error& e){
            if(std::string(e.what())=="stale cold executor mutated detailed region")throw;
            stale_rejections++;
        }
        // Test-only read-only detailed projection: immediately return ownership.
        // This does NOT exercise a real 1-million-NPC detailed-world play interval.
        const auto demote_id="resume-"+region.id+"-"+std::to_string(t);
        region.authority->prepare(demote_id,SimulationOwner::Cold,promoted.generation,
                                  promoted.checkpoint_revision,t);
        const auto demoted=region.authority->commit(demote_id,promoted.generation,
                                                   promoted.checkpoint_revision,
            [&](sqlite3* dbh,Scheduler& candidate){
                stage(dbh,region,"demote",candidate);
            });
        expect(demoted.owner==SimulationOwner::Cold&&demoted.phase==TransitionPhase::Stable,
               "demotion failed");
        expect(total_before==mass()&&ships_before==in_flight(),"handoff altered physical custody");
        visits.push_back(region.id);handoffs+=2;
    }
    std::int64_t mass()const{
        std::int64_t total=0;
        for(const auto& p:regions){
            const auto s=p->cp().scheduler;
            const auto custody=s.custody();
            total+=custody.food_kg+custody.water_kg+custody.oxygen_kg;
            for(const auto& id:{p->a,p->b}){
                const auto& c=s.colony(id);total+=life(c.habitat)+life(c.depot);
                const auto st=p->economy->site(id);
                total+=st.deposit+st.ore+st.metal+st.parts+st.tailings+st.scrap;
            }
        }
        for(const auto& f:regions[0]->economy->freight())
            if(f.stage==0)total+=f.cargo.mass();
        return total;
    }
    void invariant(std::uint64_t tick){
        expect(mass()==baseline_mass,"physical material created or lost");
        for(const auto& r:regions){
            const auto cp=r->cp();const auto st=r->mode();
            expect(cp.scheduler.tick()==tick&&st.boundary_tick==tick&&
                   st.checkpoint_revision==cp.revision&&st.owner==SimulationOwner::Cold&&
                   st.phase==TransitionPhase::Stable,"regional clock/ownership mismatch");
            for(const auto& id:{r->a,r->b}){
                const auto& c=cp.scheduler.colony(id);
                expect(c.population==PEOPLE&&c.habitat.shortages==0,"colony unsupplied or population lost");
                expect(r->economy->site(id).deposit>=0,"negative mineral reserve");
            }
        }
        const auto all=regions[0]->economy->freight();
        for(const auto& f:all)
            expect(f.arrives>f.departed&&f.returns>f.arrives,"invalid carrier route clock");
        maximum_inflight=std::max(maximum_inflight,in_flight());
    }
    void record_hour(std::uint64_t h){
        for(const auto& r:regions){
            const auto s=r->cp().scheduler;
            for(const auto& id:{r->a,r->b}){
                const auto& c=s.colony(id);const auto q=r->economy->site(id);
                csv<<h<<','<<id<<','<<c.population<<','<<c.habitat.food_kg<<','
                   <<c.habitat.water_kg<<','<<c.habitat.oxygen_kg<<','
                   <<c.depot.nutrients_kg<<','<<c.depot.food_kg<<','<<q.deposit<<','
                   <<q.ore<<','<<q.metal<<','<<q.parts<<','<<c.habitat.shortages<<','
                   <<in_flight()<<','<<mass()<<'\n';
            }
        }
        csv.flush();
    }
    void restart(std::uint64_t tick){
        const auto before=mass();
        for(auto& r:regions)r->reopen();
        expect(mass()==before,"restart corrupted physical state");
        invariant(tick);restarts++;
        expect(scalar(audit,"SELECT COUNT(*) FROM sg_cold_freight WHERE stage=0")>=0,"freight read");
    }
    void run(){
        const auto t0=std::chrono::steady_clock::now();
        invariant(0);visit(0,0);invariant(0);record_hour(0);
        for(std::uint64_t epoch=1;epoch<=32;++epoch){
            const auto t=STEP*epoch;
            process_arrivals(t);
            run_work(t);
            if(epoch%4==0){
                const auto hour=epoch/4;
                if(hour<8)dispatch_hour(hour,t);
                if(hour<=6)visit(itinerary[hour],t);
                invariant(t);
                record_hour(hour);
                if(hour==2||hour==4||hour==6)restart(t);
            }
        }
        expect(work_batches==256,"expected eight colonies times 32 work periods");
        for(const auto& id:names){
            const auto st=where(id).economy->site(id);
            expect(st.deposit==96'800'000,"finite mineral reservoir did not fund mining");
            expect(st.tailings==128'000&&st.scrap==192'000,
                   "smelting/fabrication byproducts do not reconcile");
            expect(st.last_work_tick==END,"autonomous shift cursor not durable");
        }
        if(all_to_all){
            expect(scalar(audit,"SELECT COUNT(DISTINCT origin||'-'||destination) FROM sg_cold_freight "
                                "WHERE freight_id LIKE '%network'")==56,
                   "network did not visit all 56 directed colony pairs");
        }else{
            expect(scalar(audit,"SELECT COUNT(*) FROM sg_cold_freight WHERE "
                                "SUBSTR(origin,1,1)<>SUBSTR(destination,1,1)")==0,
                   "local-only scenario leaked freight across pairs");
        }
        expect(shipments==(all_to_all?112:56),"expected hourly manifest count");
        expect(visits.size()==7,"invalid travel visit schedule");
        expect(mass()==baseline_mass,"end-of-test conservation failure");
        expect(scalar(audit,"SELECT COUNT(*) FROM pragma_integrity_check WHERE integrity_check <> 'ok'")==0,"SQLite integrity check");
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-t0).count();
        std::ofstream out(dir+"/result.json");
        out<<"{\n\"scenario\":\""<<(all_to_all?"all_pairs_trade":"only_within_pairs")<<"\",\n"
             "\"logical_hours\":8,\n\"population_total\":8000000,\n"
             "\"work_batches\":"<<work_batches<<",\n\"freight_dispatched\":"<<shipments<<",\n"
             "\"freight_arrived\":"<<arrivals<<",\n\"pending_in_transit\":"<<in_flight()<<",\n"
             "\"durable_handoffs\":"<<handoffs<<",\n\"stale_worker_rejections\":"<<stale_rejections<<",\n"
             "\"command_rejections\":"<<failed_commands<<",\n\"database_restarts\":"<<restarts<<",\n"
             "\"initial_and_final_material_mass_kg\":"<<baseline_mass<<",\n"
             "\"peak_concurrent_shipments\":"<<maximum_inflight<<",\n"
             "\"wall_clock_microseconds\":"<<us<<",\n\"visits\":[";
        for(std::size_t i=0;i<visits.size();++i){if(i)out<<",";out<<'"'<<visits[i]<<'"';}
        out<<"],\n\"detailed_mode_is_synthetic\":true\n}\n";
        std::cout<<(all_to_all?"NETWORK":"PAIRED")<<" PASS: 8 hours, "<<work_batches
                 <<" real stock work batches, "<<shipments<<" freights, "<<arrivals
                 <<" arrivals, 14 authority flips; mass="<<baseline_mass
                 <<", wall="<<static_cast<double>(us)/1e6<<" s\n";
    }
};
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::invalid_argument("usage: eight-colony-scenarios paired|network OUTPUT_DIRECTORY");
        const std::string mode=argv[1];
        if(mode!="paired"&&mode!="network")throw std::invalid_argument("invalid scenario mode");
        ScenarioRun sim(argv[2],mode=="network");sim.run();return 0;
    }catch(const std::exception& e){std::cerr<<"SCENARIO FAILED: "<<e.what()<<'\n';return 1;}
}
