#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace galaxy {

DependentIncidentAudit PersistentWorld::audit_dependent_incident(
    const std::string& family_scope, std::uint64_t original_transport,
    const std::vector<std::uint64_t>& expected_children) const {
    if(family_scope.empty() || original_transport==0 || expected_children.empty() ||
       expected_children.size()>15 ||
       !std::is_sorted(expected_children.begin(), expected_children.end()) ||
       std::adjacent_find(expected_children.begin(),expected_children.end())!=
           expected_children.end())
        throw std::invalid_argument("incident audit needs complete sorted child manifest");

    const auto records=dependent_travel(family_scope);
    std::map<std::uint64_t,std::vector<const DependentTravelRecord*>> by_child;
    std::vector<std::uint64_t> original_ids;
    for(const auto& leg:records){
        by_child[leg.child_id].push_back(&leg);
        if(leg.transport_id==original_transport)
            original_ids.push_back(leg.child_id);
    }
    std::sort(original_ids.begin(),original_ids.end());
    if(original_ids!=expected_children)
        throw std::runtime_error("incident manifest differs from durable child berths");

    DependentIncidentAudit audit{};
    audit.world_generation=generation();
    audit.initial_transport=original_transport;
    for(const auto child:expected_children){
        const auto& legs=by_child.at(child);
        const DependentTravelRecord* first=nullptr;
        for(const auto* leg:legs)
            if(leg->transport_id==original_transport){
                if(first)throw std::runtime_error("duplicate original child berth");
                first=leg;
            }
        if(!first)throw std::runtime_error("missing original child berth");
        const auto guardian=first->guardian_id;
        const auto co_guardian=first->co_guardian_id;
        const auto origin=first->origin;
        const auto destination=first->destination;
        std::set<std::uint64_t> seen;
        const auto* current=first;
        bool rescued=false;
        for(;;){
            if(!seen.insert(current->transport_id).second)
                throw std::runtime_error("dependent rescue custody cycle");
            if(current->guardian_id!=guardian ||
               current->co_guardian_id!=co_guardian ||
               current->origin!=origin || current->destination!=destination ||
               !guardian || current->ferry==0)
                throw std::runtime_error("dependent changed identity or guardians mid-voyage");
            if(current->status==1)break;
            if(current->status!=2)
                throw std::runtime_error("unsupported child custody status");
            if(current->termination_kind==0)break; // arrived at real destination
            if(current->termination_kind!=1 || !current->arrival_command)
                throw std::runtime_error("unsupported dependent incident disposition");
            const DependentTravelRecord* next=nullptr;
            for(const auto* leg:legs)
                if(leg->transfer_from_transport==current->transport_id){
                    if(next)throw std::runtime_error("forked child rescue custody");
                    next=leg;
                }
            if(!next || next->board_command!=current->arrival_command ||
               next->board_generation!=current->arrival_generation ||
               next->ferry==current->ferry)
                throw std::runtime_error("unproven dependent rescue successor");
            rescued=true;
            current=next;
        }
        // Other completed historical journeys are legitimate. A later,
        // unrelated active journey is legitimate only AFTER this journey
        // completed. Two different simultaneous active berths are not.
        if(current->status==1)
            for(const auto* leg:legs)
                if(!seen.count(leg->transport_id) && leg->status==1)
                    throw std::runtime_error("child appears aboard two voyages");
        DependentIncidentMember member{};
        member.child_id=child;
        member.guardian_id=guardian;
        member.co_guardian_id=co_guardian;
        member.initial_transport=original_transport;
        member.current_transport=current->transport_id;
        member.current_ferry=current->ferry;
        member.ever_rescued=rescued;
        if(rescued)++audit.ever_rescued;
        if(current->status==2){
            if(current->termination_kind!=0 ||
               world().voyage_status(current->transport_id)!=2)
                throw std::runtime_error("child arrival lacks completed physical voyage");
            member.custody=DependentVerifiedCustody::Arrived;
            ++audit.arrived;
        }else{
            const auto voyage_status=world().voyage_status(current->transport_id);
            if(voyage_status!=1 && voyage_status!=3)
                throw std::runtime_error("child aboard non-active ferry");
            for(const auto adult:{guardian,co_guardian}){
                if(!adult)continue;
                const auto it=world().npc_records().find(adult);
                if(it==world().npc_records().end() ||
                   it->second.location!=current->ferry ||
                   it->second.transport_id!=current->transport_id)
                    throw std::runtime_error("child separated from named guardian");
            }
            if(rescued && voyage_status==3){
                member.custody=DependentVerifiedCustody::RescuedStranded;
                ++audit.rescued_stranded;
            }else if(rescued){
                member.custody=DependentVerifiedCustody::RescuedAboard;
                ++audit.rescued_aboard;
            }else if(voyage_status==3){
                member.custody=DependentVerifiedCustody::Stranded;
                ++audit.stranded;
            }else{
                member.custody=DependentVerifiedCustody::Aboard;
                ++audit.aboard;
            }
        }
        audit.members.push_back(member);
    }
    if(audit.aboard+audit.stranded+audit.rescued_aboard+
       audit.rescued_stranded+audit.arrived !=
       expected_children.size())
        throw std::runtime_error("incident audit failed passenger conservation");
    return audit;
}

} // namespace galaxy
