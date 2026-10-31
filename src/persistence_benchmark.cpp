#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
int main(int argc, char** argv) {
    try {
        const auto path = fs::temp_directory_path() / "single_galaxy_b2_benchmark.sqlite";
        if (fs::exists(path)) { std::cerr << "Refusing existing benchmark database\n"; return 2; }
        const int num_asteroids = argc > 1 ? std::stoi(argv[1]) : 100;
        const int steps = argc > 2 ? std::stoi(argv[2]) : 100;
        if (num_asteroids < 1 || num_asteroids > 10000 || steps < 1 || steps > 2000) return 2;
        try {
            galaxy::Simulation initial(123);
            initial.add_ship(1, {0,0}, 1000000);
            for(int i=0;i<num_asteroids;i++) initial.add_asteroid({3000+i,0},1000);
            const auto started=Clock::now();
            auto w=std::make_unique<galaxy::PersistentWorld>(path.string(), std::move(initial));
            const auto initialized=Clock::now();
            std::vector<double> durations;
            durations.reserve(steps);
            for (int i=0;i<steps;i++) {
                const auto t0=Clock::now();
                w->step();
                durations.push_back(std::chrono::duration<double,std::micro>(Clock::now()-t0).count());
            }
            const auto stop=Clock::now();
            const auto final_hash=w->world().state_hash();
            w.reset();
            galaxy::PersistentWorld reopened(path.string(),galaxy::Simulation(22));
            if (reopened.world().state_hash()!=final_hash || reopened.world().tick()!=static_cast<unsigned>(steps)) return 1;
            std::sort(durations.begin(),durations.end());
            auto pct=[&](double p){ return durations[static_cast<std::size_t>(p*(durations.size()-1))]; };
            std::cout<<"asteroids,steps,init_us,step_p50_us,step_p95_us,step_p99_us,step_max_us,elapsed_ms,db_bytes,final_hash\n";
            std::cout<< num_asteroids << ',' << steps << ',' << std::fixed << std::setprecision(2)
                     << std::chrono::duration<double,std::micro>(initialized-started).count() << ','
                     << pct(.50)<<','<<pct(.95)<<','<<pct(.99)<<','<<durations.back()<<','
                     << std::chrono::duration<double,std::milli>(stop-started).count()<<','
                     << fs::file_size(path)<<','<<final_hash<<'\n';
        } catch(...) { fs::remove(path); throw; }
        fs::remove(path);
        return 0;
    } catch (const std::exception& e) { std::cerr<<"benchmark failed: "<<e.what()<<'\n'; return 1; }
}
