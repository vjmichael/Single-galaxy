#include "galaxy/CanonicalShippingHost.hpp"
#include "galaxy/RegionalShipmentReconciliation.hpp"
#include <stdexcept>
#include <utility>
namespace galaxy::shipping {
CanonicalShippingHost::CanonicalShippingHost(PersistentWorld& w,
 regional_events::CheckpointStore& region,regional_events::RegionalModeAuthority& owner,
 industry::RegionalProductionNetwork& network,ThirdPartyRegistry& registry,
 std::string database,SessionAuthentication auth)
 :world_(w),region_(region),owner_(owner),network_(network),registry_(registry),
  shipping_host_(w,network,registry,database),path_(std::move(database)),authenticate_(std::move(auth)){
 if(!authenticate_)throw std::invalid_argument("canonical shipping host requires trusted session authentication");
 if(!region_.load())throw std::runtime_error("canonical shipping host requires persisted regional checkpoint");
 verify();
}
void CanonicalShippingHost::admission(PlayerId principal) const {
 if(principal==0||!authenticate_(principal))throw std::runtime_error("unauthenticated game-host principal");
}
void CanonicalShippingHost::require_stable_region() const {
 if(owner_.state().phase!=regional_events::TransitionPhase::Stable)
  throw std::runtime_error("region ownership handoff freezes shipping command admission");
}
bool CanonicalShippingHost::player_command(const Command& c,PlayerId session,FailPoint fail){
 admission(session);
 if(c.issuer!=session)throw std::runtime_error("player command principal differs from authenticated session");
 require_stable_region();
 return world_.submit(c,fail); // host-only freight action is NOT exposed by submit
}
void CanonicalShippingHost::book(const ThirdPartyBooking& b,PlayerId session){
 admission(session);require_stable_region();if(session!=b.source_owner)throw std::runtime_error("foreign warehouse booking principal");
 shipping_host_.book_factory_goods(b);
}
void CanonicalShippingHost::dispatch(const std::string& id,PlayerId carrier,const std::string& dispatch_id){
 admission(carrier);require_stable_region();shipping_host_.dispatch_factory_goods(id,carrier,dispatch_id);
}
void CanonicalShippingHost::load(const std::string& id,PlayerId carrier,FailPoint fail){
 admission(carrier);require_stable_region();registry_.load(id,fail);shipping_host_.verify();
}
void CanonicalShippingHost::propose_redirect(const std::string& id,PlayerId carrier,EntityId ship,
 EntityId site,PlayerId receiver,std::uint32_t system,std::uint64_t command){
 admission(carrier);require_stable_region();registry_.propose_alternate(id,carrier,ship,site,receiver,system,command);
}
void CanonicalShippingHost::approve_redirect(const std::string& id,PlayerId principal,int role){
 admission(principal);require_stable_region();registry_.approve_alternate(id,principal,role);
}
void CanonicalShippingHost::deliver_redirected(const std::string& id,PlayerId carrier,FailPoint fail){
 admission(carrier);require_stable_region();shipping_host_.deliver_redirected_goods(id,fail);
}
regional_events::RegionalAuthorityState CanonicalShippingHost::transition_region(
 const std::string& id,regional_events::SimulationOwner target){
 auto state=owner_.state();
 if(state.phase!=regional_events::TransitionPhase::Stable){
   const auto matching=target==regional_events::SimulationOwner::Detailed?
     regional_events::TransitionPhase::ToDetailed:regional_events::TransitionPhase::ToCold;
   if(state.phase!=matching||state.handoff_id!=id)
     throw std::runtime_error("stale or conflicting prepared region handoff");
   // Restart of the exact durable intent: the regional prepare already
   // fenced old ownership, so do not demand a stable-owner preflight.
   verify_regional_shipment_boundaries(path_);
 }else{
   if(state.owner==target)throw std::runtime_error("invalid same-owner cutover");
   verify();
 }
 auto result=target==regional_events::SimulationOwner::Detailed?
   network_.promote(id):network_.demote(id);
 verify();
 return result;
}
void CanonicalShippingHost::verify() const {
 shipping_host_.verify();
 verify_regional_shipment_boundaries(path_);
 const auto o=owner_.state(); const auto cp=region_.load();
 if(!cp||o.checkpoint_revision!=cp->revision)
  throw std::runtime_error("game host regional authority/checkpoint divergence");
}
} // namespace galaxy::shipping
