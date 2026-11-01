#pragma once
// ECON-02B2d4c1: host-owned authorization and durable lifecycle for editor
// .sglayout imports. A browser blueprint is DATA, never execution authority.
#include "galaxy/BlueprintConstruction.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace galaxy::industry {
struct HostedPlanStatus {
    std::int64_t plan_id=0;
    PlayerId owner=0;
    EntityId ship=0,site=0;
    ConstructionProgress progress{}; // always derived from authoritative world
};
class HostBlueprintRegistry {
public:
    // TRUSTED HOST ONLY: sqlite_path must refer to the same PersistentWorld.
    // World mutations and plan mutations must be serialized by the host.
    HostBlueprintRegistry(const std::string& sqlite_path, PersistentWorld& world);
    ~HostBlueprintRegistry();
    HostBlueprintRegistry(const HostBlueprintRegistry&)=delete;
    HostBlueprintRegistry& operator=(const HostBlueprintRegistry&)=delete;

    // Owner and ship are supplied by the authenticated HOST session, not read
    // out of the untrusted .sglayout. Returns immutable, host-issued plan ID.
    std::int64_t register_layout(const std::string& canonical_layout,
                  PlayerId authenticated_owner,EntityId authenticated_ship,EntityId site);
    HostedPlanStatus status(std::int64_t plan_id,PlayerId authenticated_owner) const;
    // Cancellation is durable and irreversible. It does not refund constructed
    // equipment or overwrite world construction effects.
    bool cancel(std::int64_t plan_id,PlayerId authenticated_owner);
    // Issues at most one NEW command ID in a reserved host namespace.
    // The actual effects are committed only by PersistentWorld.
    Event execute_one(std::int64_t plan_id,PlayerId authenticated_owner);
private:
    class Db;
    std::unique_ptr<Db> db_;
    PersistentWorld& world_;
};
} // namespace galaxy::industry
