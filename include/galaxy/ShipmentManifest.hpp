#pragma once
#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <cstdint>
#include <functional>
#include <string>

namespace galaxy::industry { class RegionalProductionNetwork; }
namespace galaxy::shipping {
// v55: host-controlled, single-lot manifests for the existing physically owned
// constructed-storage/colony-ship transfer path. Not a remote teleport or a
// third-party ownership-transfer feature. World resource ledger is the receipt.
struct Booking {
    std::string id;
    EntityId ship=0, origin_site=0;
    PlayerId pilot=0;
    std::uint32_t destination_system=0;
    int item_index=0; // 1 panels, 2 frames
    std::int64_t quantity_kg=0;
    std::uint64_t factory_return_command=0, load_command=0, unload_command=0;
};

// Called by PersistentWorld submit and recover: reservation gate prevents other
// command IDs from touching a booked ship's corresponding cargo stack.
bool manifest_transfer_authorized(sqlite3* db, const Command& cmd);
void verify_manifest_registry(const std::string& path);
// v56: second physical custody path for independently owned host-validated
// warehouse/ship/receiver. Existing v55 owner-bound commands remain untouched.
bool third_party_transfer_authorized(sqlite3* db, const Command& cmd);
void verify_third_party_registry(const std::string& path);
struct ThirdPartyBooking {
 std::string id;
 EntityId source_ship=0, carrier_ship=0, receiving_ship=0;
 EntityId origin_site=0, destination_site=0;
 PlayerId source_owner=0, carrier_owner=0, receiving_owner=0;
 std::uint32_t destination_system=0;
 int item_index=0;
 std::int64_t quantity_kg=0;
 std::uint64_t factory_return_command=0, load_command=0, unload_command=0;
 bool require_dispatch=false; // v57 opt-in: atomic booking with durable dispatch gate
};
class ThirdPartyRegistry {
public:
 ThirdPartyRegistry(PersistentWorld& world, std::string path, std::function<bool(PlayerId)> authenticate);
 // Requires three independently identified world owners and constructed
 // warehouses. Upstream ingress must authenticate each player before calls.
 void book(const ThirdPartyBooking& b);
 // The host MUST authenticate the active principal before accepting consent.
 // Roles: 1 = source warehouse owner; 2 = carrier; 3 = destination owner.
 void approve(const std::string& id, PlayerId principal, int role);
 // Revoke only before physical loading, never discard goods already in transit.
 void revoke(const std::string& id, PlayerId principal);
 void load(const std::string& id, FailPoint fail=FailPoint::None);
 void unload(const std::string& id, FailPoint fail=FailPoint::None);
 // v57: host-authenticated dispatch after three separate owner approvals.
 void dispatch(const std::string& id, PlayerId carrier_principal, const std::string& dispatch_id);
 // After physical loading, all three owners must approve the alternate
 // physical return-to-origin route. No free remote rerouting or teleport.
 void request_return(const std::string& id, PlayerId requester, std::uint64_t return_command);
 void approve_return(const std::string& id, PlayerId principal, int role);
 void return_to_origin(const std::string& id, FailPoint fail=FailPoint::None);
 // Both actual custody endpoints attest the already-committed world receipt.
 // Success: carrier + receiver. Return: source + carrier. No money moves.
 void acknowledge_settlement(const std::string& id, PlayerId principal, int role);
 // v58: replace an undelivered destination with a new constructed, independently
 // owned destination. Requires all three original roles AND the new receiver.
 void propose_alternate(const std::string& id, PlayerId principal,
       EntityId receiving_ship, EntityId destination_site, PlayerId receiving_owner,
       std::uint32_t destination_system, std::uint64_t unload_command);
 void approve_alternate(const std::string& id, PlayerId principal, int role);
 void unload_alternate(const std::string& id, FailPoint fail=FailPoint::None);
 // Exact physical receipt settled by carrier (2) and alternate receiver (4).
 void acknowledge_alternate_settlement(const std::string& id, PlayerId principal, int role);
 void verify() const;
private:
 PersistentWorld& world_; std::string path_; std::function<bool(PlayerId)> authenticate_;
};

// Focused production-host boundary: network verification plus source-world
// provenance and booking admission. This does not bypass the regional
// manufacturing authority or create phantom finished goods.
class ProductionShippingHost {
public:
 ProductionShippingHost(PersistentWorld& world,
     industry::RegionalProductionNetwork& network, ThirdPartyRegistry& shipping,
     std::string database);
 void book_factory_goods(const ThirdPartyBooking& booking);
 void dispatch_factory_goods(const std::string& id, PlayerId carrier, const std::string& dispatch_id);
 void deliver_redirected_goods(const std::string& id, FailPoint fail=FailPoint::None);
 void verify() const;
private:
 PersistentWorld& world_;
 industry::RegionalProductionNetwork& network_;
 ThirdPartyRegistry& shipping_;
 std::string path_;
};

class ShipmentRegistry {
public:
    ShipmentRegistry(PersistentWorld& world, std::string path);
    void book(const Booking& booking);
    // Destination is built and commissioned by the source ship upon arrival.
    void designate_destination(const std::string& manifest_id, EntityId site);
    // Runs the exact pre-committed world command. Retry after interrupted step
    // is safe: the canonical receipt, not a shadow mutable status, decides state.
    void load(const std::string& manifest_id, FailPoint fail=FailPoint::None);
    void unload(const std::string& manifest_id, FailPoint fail=FailPoint::None);
    // Reconcile against persisted world effects, not mere manifest strings.
    void verify() const;
private:
    PersistentWorld& world_;
    std::string path_;
};
} // namespace galaxy::shipping
