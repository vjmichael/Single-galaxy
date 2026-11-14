#pragma once
// AUTO-04D.4B: a read-only, deterministic WHOLE colony manifest.
// This proves candidate identity/counts and local receiving capacity only.
// It does not create transport reservations, guardian consent or a departure.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <map>
#include <vector>
namespace galaxy::colony_evacuation {
struct ReceiverCandidate {
    EntityId ship=0;
    std::uint64_t vacant_passenger_berths=0;
    std::uint64_t funded_two_period_berths=0;
    bool locally_accessible=false;
};
struct Manifest {
    EntityId home_ship=0;
    std::uint64_t world_tick=0;
    std::uint64_t declared_population=0;
    std::vector<std::uint64_t> named_people;
    // Each named person's current authoritative location; no virtual clones.
    std::map<EntityId,std::uint64_t> people_by_physical_location;
    std::vector<ReceiverCandidate> receiving_vessels;
    bool roster_complete=false;
    bool physically_reconciled=false;
    bool sufficient_local_berths=false;
    // Separate world and ART02/ECON03 atomic adapters are NOT yet implemented.
    bool dependent_guardianship_verified=false;
    bool last_operating_crew_departure_supported=false;
    bool executable=false;
};
Manifest survey(const Simulation& world,EntityId home_ship);
}
