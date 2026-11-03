#pragma once
// DEMOG-05q6e5b8j: read-only, principal-scoped projection of the canonical
// immutable freight registry. No second inventory balance or mutable status.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace galaxy::shipping {
struct ShipmentStatus {
 std::string id,material,phase,role;
 std::uint64_t source_site=0,destination_site=0,carrier_ship=0;
 std::uint64_t source_owner=0,carrier_owner=0,receiver_owner=0;
 std::int64_t quantity_kg=0;
 bool dispatched=false,loaded=false,delivered=false,returned=false,settled=false,redirected=false,revoked=false;
 bool my_approval_recorded=false,my_settlement_recorded=false;
};
class ShipmentStatusView {
public:
 explicit ShipmentStatusView(std::string database);
 // No client-supplied filter IDs: all rows are constrained to the trusted
 // server-resolved principal, including a fourth-party alternate receiver.
 std::vector<ShipmentStatus> for_principal(PlayerId principal) const;
 static std::string to_json(const std::vector<ShipmentStatus>& rows);
private:
 std::string database_;
};
} // namespace galaxy::shipping
