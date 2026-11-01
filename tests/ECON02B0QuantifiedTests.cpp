#include "galaxy/QuantifiedProduction.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
using namespace galaxy::industry;
static void ok(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static void rejects(const std::function<void()> &f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}ok(threw,"invalid action was accepted");}
static Recipe panel(){return {"Structural panel","cut",{{"aluminum",12000},{"sheet",8000}},
                             {"panel",19000},{"cutting_scrap",1000}};}
static State stock(std::int64_t raw=120000,std::int64_t sheet=80000,
                   std::int64_t finish_capacity=200000,std::int64_t scrap_capacity=20000){
    State s; s.feed.capacity_grams=10000000; s.finished.capacity_grams=finish_capacity;
    s.scrap.capacity_grams=scrap_capacity;
    if(raw)s.feed.deposit("aluminum",raw);
    if(sheet)s.feed.deposit("sheet",sheet);
    return s;
}
static bool equal(const State& a,const State& b){
    return a.feed.grams==b.feed.grams && a.finished.grams==b.finished.grams &&
        a.scrap.grams==b.scrap.grams && a.has_work_in_progress==b.has_work_in_progress &&
        a.ticks_remaining==b.ticks_remaining && a.elapsed_ticks==b.elapsed_ticks &&
        a.completed_batches==b.completed_batches &&
        a.reserved_inputs.size()==b.reserved_inputs.size() &&
        (a.reserved_inputs.empty() ||
         (a.reserved_inputs[0].code==b.reserved_inputs[0].code &&
          a.reserved_inputs[0].grams==b.reserved_inputs[0].grams &&
          a.reserved_inputs.back().code==b.reserved_inputs.back().code &&
          a.reserved_inputs.back().grams==b.reserved_inputs.back().grams));
}
int main(){try{
    const auto r=panel();
    { // both ingredients must meet the minimum: no output, no scrap, no debit
        auto s=stock(12000,7999); QuantifiedLine line(r,{1,"cut"},s);
        const auto before=line.total_mass_grams();
        auto x=line.advance(1000000);
        ok(x.completed_batches==0 && x.blockage==Blockage::InsufficientInput,"subminimum job fabricated goods");
        ok(equal(line.state(),State{stock(12000,7999).feed, stock(12000,7999).finished,
                                     stock(12000,7999).scrap,false,0,{},1000000,0}),"shortage mutated resources");
        ok(line.total_mass_grams()==before,"shortage fabricated/lost mass");
        line.advance(1);ok(line.state().completed_batches==0,"shortage generated output after time");
    }
    { // exact min, partial WIP, pause, durable snapshot, restart
        QuantifiedLine first(r,{1,"cut"},stock(12000,8000));
        const auto before=first.total_mass_grams();
        first.advance(2);
        ok(first.state().has_work_in_progress && first.state().ticks_remaining==3,"missing in-progress reservation");
        ok(first.state().finished.used()==0 && first.state().scrap.used()==0,"partial operation fabricated output/scrap");
        ok(first.total_mass_grams()==before,"WIP not backed by mass");
        QuantifiedLine restored(r,{1,"cut"},first.state());
        restored.set_power(false);restored.advance(500);
        ok(restored.state().ticks_remaining==3,"brownout advanced WIP");
        restored.set_power(true);restored.advance(3);
        ok(restored.state().completed_batches==1 && restored.state().finished.amount("panel")==19000 &&
            restored.state().scrap.amount("cutting_scrap")==1000,"restart did not finish exactly one batch");
        ok(restored.total_mass_grams()==before,"mass conservation failure after recovery");
    }
    { // no place for both product AND waste means no start and no debit
        QuantifiedLine outfull(r,{1,"cut"},stock(120000,80000,18999,20000));
        outfull.advance(100); ok(outfull.state().completed_batches==0 &&
            outfull.state().feed.amount("aluminum")==120000 && outfull.state().scrap.used()==0,
            "output backpressure did not block");
        QuantifiedLine scrapfull(r,{1,"cut"},stock(120000,80000,200000,999));
        scrapfull.advance(100);ok(scrapfull.state().completed_batches==0 &&
            scrapfull.state().feed.amount("sheet")==80000 && scrapfull.state().finished.used()==0,
            "scrap backpressure did not block");
    }
    { // large aggregated remote step equals incremental nearby steps
        QuantifiedLine distant(r,{1,"cut"},stock());
        QuantifiedLine nearby(r,{1,"cut"},stock());
        const auto mass=distant.total_mass_grams();
        distant.advance(1000);
        for(int i=0;i<1000;++i)nearby.advance(1);
        ok(equal(distant.state(),nearby.state()),"remote advancement disagrees with near simulation");
        ok(distant.state().completed_batches==10,"expected bounded ten batches");
        ok(distant.total_mass_grams()==mass,"aggregate remote advance broke conservation");
    }
    { // Tier 2 needs 4 recipe quantities, not 1; full output at 6 ticks
        QuantifiedLine t2(r,{2,"cut"},stock(4*12000,4*8000));
        t2.advance(100);
        ok(t2.state().completed_batches==1 && t2.state().finished.amount("panel")==76000 &&
            t2.state().scrap.amount("cutting_scrap")==4000,"tier-2 batch scaling wrong");
        QuantifiedLine short_t2(r,{2,"cut"},stock(4*12000,4*8000-1));
        short_t2.advance(100);ok(short_t2.state().completed_batches==0 &&
            short_t2.state().scrap.used()==0,"tier minimum not enforced");
    }
    { // A finished WIP waits for storage to clear without duplicating a batch
        QuantifiedLine line(r,{1,"cut"},stock(24000,16000,38000,2000));
        line.advance(2);
        line.advance(3);
        ok(line.state().completed_batches==1,"first batch didn't finish");
        // Capacity for second output disappears while it is already in progress.
        line.advance(1); // reserve second batch, one tick completed
        line.state();
        // Can't externally modify const state; reconstruct a valid snapshot with a
        // physically delivered third-party item in the shared output storage.
        State intervening=line.state();intervening.finished.deposit("unrelated",19000);
        QuantifiedLine changed(r,{1,"cut"},intervening);
        const auto mass=changed.total_mass_grams();
        changed.advance(20);
        ok(changed.state().has_work_in_progress && changed.state().ticks_remaining==0 &&
            changed.state().completed_batches==1,"blocked WIP vanished or finished without space");
        State freed=changed.state();freed.finished.withdraw("unrelated",19000);
        QuantifiedLine resumed(r,{1,"cut"},freed);
        resumed.advance(1);
        ok(resumed.state().completed_batches==2,"blocked WIP did not resume after capacity freed");
        ok(resumed.total_mass_grams()==mass-19000,"delivered stock not correctly removed");
    }
    { // Staff and maintenance gates must never count elapsed idle time as production.
        QuantifiedLine line(r,{1,"cut"},stock(24000,16000));
        const auto original=line.total_mass_grams();
        line.set_staff(false);line.advance(100000);
        ok(line.state().completed_batches==0 && line.state().scrap.used()==0 &&
            line.state().feed.amount("aluminum")==24000,"unavailable worker produced goods");
        line.set_staff(true);line.set_maintenance(false);line.advance(100000);
        ok(line.state().completed_batches==0 && line.state().scrap.used()==0,
            "broken machine produced goods");
        line.set_maintenance(true);line.advance(10);
        ok(line.state().completed_batches==2 && line.total_mass_grams()==original,
            "idle time accumulated free production");
    }
    { // Large Tier-5 processing respects a machine-scaled minimum batch.
        QuantifiedLine enormous(r,{5,"cut"},stock(3072000,2048000,4864000,256000));
        const auto before=enormous.total_mass_grams();
        enormous.advance(5);
        ok(enormous.state().completed_batches==0 && enormous.state().scrap.used()==0,
            "tier-5 operation completed early");
        enormous.advance(1);
        ok(enormous.state().completed_batches==1 && enormous.state().finished.amount("panel")==4864000 &&
            enormous.state().scrap.amount("cutting_scrap")==256000 &&
            enormous.total_mass_grams()==before,"tier-5 standard batch not conserved");
        QuantifiedLine short_enormous(r,{5,"cut"},stock(3072000,2047999,4864000,256000));
        short_enormous.advance(1000000);
        ok(short_enormous.state().finished.used()==0 && short_enormous.state().scrap.used()==0,
            "tier-5 subminimum generated scrap or output");
    }
    { // Fail closed on invalid recipes and corrupt saved WIP
        auto bad=panel();bad.scrap.grams=0;
        rejects([&]{QuantifiedLine x(bad,{1,"cut"},stock());});
        rejects([&]{QuantifiedLine x(r,{6,"cut"},stock());});
        auto badstate=stock();badstate.has_work_in_progress=true;
        rejects([&]{QuantifiedLine x(r,{1,"cut"},badstate);});
        rejects([&]{QuantifiedLine x(r,{1,"smelt"},stock());});
    }
    std::cout<<"ECON02B0: minimum batches, multi-ingredient shortage, no-shortage scrap, tier scaling, storage backpressure, power, WIP restore, remote equivalence, conservation: PASS\n";
}catch(const std::exception& ex){std::cerr<<"ECON02B0 FAILED: "<<ex.what()<<'\n';return 1;}}
