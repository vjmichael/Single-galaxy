#pragma once
// ECON-03c5 — host-only authorization and idempotent regional command adapter.
// This is NOT a network endpoint or an NPC physical job/route validator.
#include "galaxy/RegionalCheckpointStore.hpp"
#include <functional>
#include <string>

namespace galaxy::regional_events {
class CustodyLedger;
enum class HostOp { ScheduleShipment, ScheduleHabitatDelivery, SchedulePopulation,
                    Advance, AcknowledgeNotices };
struct HostCommand {
    std::string command_id;           // stable retry key, scoped per region
    std::string actor;                // authenticated by owning host, never client asserted
    HostOp op=HostOp::Advance;
    std::string origin, destination;
    std::string carrier_receipt;     // host policy checks physical carrier/route
    Cargo cargo{};
    std::uint64_t at_tick=0, travel_ticks=0, population=0, expected_notice_sequence=0;
};
// Host policy must verify authenticated actor, local facility ownership,
// completed NPC handoff/carrier receipt and route for the relevant operation.
// No untrusted clients may set this policy or get direct CheckpointStore access.
using HostPolicy=std::function<bool(const HostCommand&)>;
class HostCommands {
public:
    HostCommands(CheckpointStore& store,HostPolicy policy,
                 CustodyLedger* custody=nullptr);
    // Enrolled regions also require the current durable cold-owner generation.
    // An old paused worker cannot write after authority has changed hands.
    CommandReceipt execute(const HostCommand& cmd,std::uint64_t expected_revision,
                           std::uint64_t expected_owner_generation=0);
private:
    CheckpointStore& store_;
    HostPolicy policy_;
    CustodyLedger* custody_=nullptr;
};
} // namespace galaxy::regional_events
