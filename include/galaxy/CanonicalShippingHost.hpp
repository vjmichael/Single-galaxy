#pragma once
// DEMOG-05q6e5b8i: in-process host integration boundary.
// This authenticates a native host principal, never a claimed client username.
// The production world, manufacturing network and cargo registry remain
// the only inventory/owner authorities. Not a remote multiplayer endpoint.
#include "galaxy/RegionalProductionNetwork.hpp"
#include "galaxy/ShipmentManifest.hpp"
#include <functional>
#include <string>
namespace galaxy::shipping {
class CanonicalShippingHost {
public:
 using SessionAuthentication=std::function<bool(PlayerId)>;
 CanonicalShippingHost(PersistentWorld&,regional_events::CheckpointStore&,
   regional_events::RegionalModeAuthority&,industry::RegionalProductionNetwork&,
   ThirdPartyRegistry&,std::string database,SessionAuthentication);
 // Never forward a player-stated owner identity unchecked to the world.
 bool player_command(const Command&,PlayerId session_principal,FailPoint=FailPoint::None);
 void book(const ThirdPartyBooking&,PlayerId session_principal);
 // Player-facing shipping lifecycle: only host-validated principals enter here.
 void approve_booking(const std::string&,PlayerId principal,int role);
 void revoke_booking(const std::string&,PlayerId principal);
 void dispatch(const std::string&,PlayerId carrier,const std::string& dispatch_id);
 void load(const std::string&,PlayerId carrier,FailPoint=FailPoint::None);
 void unload(const std::string&,PlayerId carrier,FailPoint=FailPoint::None);
 void request_return(const std::string&,PlayerId principal,std::uint64_t return_command);
 void approve_return(const std::string&,PlayerId principal,int role);
 void return_to_origin(const std::string&,PlayerId carrier,FailPoint=FailPoint::None);
 void acknowledge_delivery(const std::string&,PlayerId principal,int role);
 void propose_redirect(const std::string&,PlayerId carrier,EntityId receiving_ship,
   EntityId destination_site,PlayerId new_receiver,std::uint32_t system,std::uint64_t command);
 void approve_redirect(const std::string&,PlayerId principal,int role);
 void deliver_redirected(const std::string&,PlayerId carrier,FailPoint=FailPoint::None);
 void acknowledge_redirected_delivery(const std::string&,PlayerId principal,int role);
 // Trusted host only. Must not be exposed as an untrusted client command.
 regional_events::RegionalAuthorityState transition_region(const std::string& handoff_id,
   regional_events::SimulationOwner target);
 void verify() const;
private:
 void admission(PlayerId principal) const;
 void require_stable_region() const;
 PersistentWorld& world_;
 regional_events::CheckpointStore& region_;
 regional_events::RegionalModeAuthority& owner_;
 industry::RegionalProductionNetwork& network_;
 ThirdPartyRegistry& registry_;
 ProductionShippingHost shipping_host_;
 std::string path_;
 SessionAuthentication authenticate_;
};
} // namespace galaxy::shipping
