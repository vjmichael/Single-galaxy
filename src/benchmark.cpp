#include "galaxy/Simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

using Clock = std::chrono::steady_clock;

int main() {
    constexpr std::uint64_t kWarmup = 500;
    constexpr std::uint64_t kMeasured = 3000;
    std::cout << "entities,warmup_ticks,measured_ticks,p50_us,p95_us,p99_us,worst_us,final_state_hash\n";
    for (int n : {1, 10, 100, 1000}) {
        galaxy::Simulation sim(20261030);
        for (int i = 0; i < n; ++i) {
            sim.add_ship(static_cast<std::uint64_t>(i + 1), {i * 10, -i * 10}, 50);
        }
        sim.steps(kWarmup);
        std::vector<double> measured;
        measured.reserve(kMeasured);
        for (std::uint64_t i = 0; i < kMeasured; ++i) {
            const auto start = Clock::now();
            sim.step();
            const auto end = Clock::now();
            measured.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        }
        std::sort(measured.begin(), measured.end());
        auto quantile = [&](double q) { return measured[static_cast<std::size_t>(q * (measured.size() - 1))]; };
        std::cout << n << ',' << kWarmup << ',' << kMeasured << ','
                  << std::fixed << std::setprecision(4)
                  << quantile(0.50) << ',' << quantile(0.95) << ','
                  << quantile(0.99) << ',' << measured.back() << ','
                  << sim.state_hash() << '\n';
    }
}
