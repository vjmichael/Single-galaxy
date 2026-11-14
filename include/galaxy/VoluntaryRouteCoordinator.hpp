#pragma once
// EXIT-01E.A: bounded, host-only coordination of PRE-AUTHENTICATED routes.
// This is not an immigration consent service, transport simulator or durable
// ticket ledger. All physical custody is committed by EXIT-01D/01B.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace galaxy::colony_routes {
struct Offer {
    std::uint64_t origin=0, destination=0, ferry=0;
    std::uint64_t receiving_owner=0, source_owner=0;
    std::uint64_t generation=0, expiry_tick=0;
    std::uint64_t admission_id=0, transport_id=0, boarding_id=0, pressure_id=0;
    std::uint32_t seats=0, occupied_seats=0;
};
struct Request {
    std::uint64_t origin=0, source_owner=0, generation=0, tick=0;
    std::uint32_t passengers=1;
};
enum class Status { NoOffers, NoAuthenticatedRoute, NoCapacity, Dispatched };
struct Result {
    Status status=Status::NoOffers;
    std::uint64_t destination=0, ferry=0, admission_id=0;
    std::uint32_t examined=0;
};
// Calls to broker_authenticate must bind the ORIGINAL immutable offer to
// real receiving-owner authorization, including its epoch and identity.
// Calls to physical_preflight must check real vessel/system/custody and policy.
// No client-controlled boolean or owner integer is proof of either.
class Coordinator {
public:
    using Check=std::function<bool(const Offer&)>;
    using Dispatch=std::function<bool(const Offer&)>;
    Result coordinate(const Request& request, std::vector<Offer> offers,
                      const Check& broker_authenticate,
                      const Check& physical_preflight,
                      const Dispatch& native_admit_and_queue) const {
        if (!request.origin || !request.source_owner || !request.generation ||
            !request.passengers || request.passengers>32 ||
            !broker_authenticate || !physical_preflight || !native_admit_and_queue)
            throw std::invalid_argument("invalid host route request or callbacks");
        Result result;
        if (offers.empty()) return result;
        // Deterministic ordering across host restarts, independent of input
        // container order; bounded work rejects arbitrary massive offer lists.
        if (offers.size()>64) throw std::invalid_argument("route offer budget exceeded");
        std::sort(offers.begin(),offers.end(),[](const Offer& a,const Offer& b){
            return std::tie(a.destination,a.ferry,a.admission_id,a.transport_id) <
                   std::tie(b.destination,b.ferry,b.admission_id,b.transport_id);
        });
        result.status=Status::NoAuthenticatedRoute;
        std::set<std::uint64_t> seen_admissions,seen_transport,seen_boarding;
        std::set<std::pair<std::uint64_t,std::uint64_t>> seen_ferry_routes;
        for(const auto& o:offers) {
            ++result.examined;
            // Stale permissions, same-owner disguised immigration and missing
            // unique identifiers are refused BEFORE any callback effects.
            if(o.origin!=request.origin || o.source_owner!=request.source_owner ||
               o.receiving_owner==request.source_owner ||
               !o.destination || !o.ferry || !o.receiving_owner ||
               !o.admission_id || !o.transport_id || !o.boarding_id ||
               o.generation!=request.generation || o.expiry_tick<request.tick)
                continue;
            // Prevent two competing offers for the same ferry route and
            // different manifests from silently overwriting one another.
            if(!seen_admissions.insert(o.admission_id).second ||
               !seen_transport.insert(o.transport_id).second ||
               !seen_boarding.insert(o.boarding_id).second ||
               !seen_ferry_routes.emplace(o.ferry,o.destination).second)
                continue;
            if(!broker_authenticate(o)) continue;
            if(o.occupied_seats>o.seats ||
               request.passengers>o.seats-o.occupied_seats) {
                result.status=Status::NoCapacity;
                continue;
            }
            if(!physical_preflight(o)) continue;
            // The authoritative native callback must durably accept/queue
            // EXIT-01D's exact admission, or report false without claiming
            // a booking. No reservation is fabricated here on failure.
            if(!native_admit_and_queue(o)) continue;
            result.status=Status::Dispatched;
            result.destination=o.destination;
            result.ferry=o.ferry;
            result.admission_id=o.admission_id;
            return result;
        }
        return result;
    }
};
} // namespace galaxy::colony_routes
