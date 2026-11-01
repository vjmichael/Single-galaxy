#pragma once
// ECON-03c6b — durable regional carrier/handoff manifest in the SAME SQLite
// transaction as checkpoint + command receipt. NOT yet the PersistentWorld
// inventory schema or proof of real NPC work/carrier routing.
#include "galaxy/RegionalCheckpointStore.hpp"
#include <optional>
#include <string>

namespace galaxy::regional_events {
struct HostCommand;
struct CustodyEntry {
    std::uint64_t shipment_id=0;
    std::string handoff_id,origin,destination;
    std::uint64_t departure_tick=0,arrival_tick=0;
    Cargo cargo{};
    ShipmentStage stage=ShipmentStage::Reserved;
};
class CustodyLedger {
public:
    CustodyLedger(const std::string& database_file,const std::string& scope);
    ~CustodyLedger();
    CustodyLedger(const CustodyLedger&)=delete;
    CustodyLedger& operator=(const CustodyLedger&)=delete;
    // One-time opt-in. Requires a matching checkpoint revision and no
    // outstanding legacy shipments whose physical custody cannot be proven.
    void enable(const VersionedCheckpoint& checkpoint);
    bool enabled() const;
    const std::string& scope() const noexcept {return scope_;}
    std::optional<CustodyEntry> find(std::uint64_t shipment_id) const;
    // Only for trusted CheckpointStore::commit_command in its OPEN transaction.
    void record(sqlite3* same_transaction,const HostCommand& command,
                const Scheduler& before,const Scheduler& after,std::uint64_t outcome) const;
private:
    sqlite3* db_=nullptr;
    std::string scope_;
};
} // namespace galaxy::regional_events
