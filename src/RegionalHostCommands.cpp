#include "galaxy/RegionalHostCommands.hpp"
#include "galaxy/RegionalCustodyLedger.hpp"
#include <stdexcept>
#include <sstream>
#include <locale>
#include <utility>

namespace galaxy::regional_events {
namespace {
// Length-prefix all strings; avoid ambiguous concatenation and locale-sensitive
// formatting. This is for retry equivalence, NOT cryptographic authentication.
void field(std::ostringstream& o,const std::string& s){o<<s.size()<<":"<<s<<";";}
std::string canonical(const HostCommand& c){
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o<<static_cast<int>(c.op)<<";";
    field(o,c.origin);field(o,c.destination);field(o,c.carrier_receipt);
    o<<c.cargo.food_kg<<';'<<c.cargo.water_kg<<';'<<c.cargo.oxygen_kg<<';'
     <<c.at_tick<<';'<<c.travel_ticks<<';'<<c.population<<';'<<c.expected_notice_sequence<<';';
    return o.str();
}
}
HostCommands::HostCommands(CheckpointStore& store,HostPolicy policy,CustodyLedger* custody)
    :store_(store),policy_(std::move(policy)),custody_(custody){
    if(!policy_)throw std::invalid_argument("host command policy required");
    if(custody_&&custody_->scope()!=store_.scope())
        throw std::invalid_argument("regional custody ledger scope mismatch");
    if(custody_&&!custody_->enabled())
        throw std::invalid_argument("physical custody ledger must be enabled before host commands");
}
CommandReceipt HostCommands::execute(const HostCommand& cmd,std::uint64_t expected_revision,
                                      std::uint64_t expected_owner_generation){
    if(cmd.command_id.empty() || cmd.actor.empty())
        throw std::invalid_argument("regional host command identity missing");
    if(!policy_(cmd))throw std::runtime_error("regional host authorization denied");
    return store_.commit_command(cmd.command_id,cmd.actor,canonical(cmd),expected_revision,
        [&cmd](Scheduler& scheduler)->std::uint64_t{
            switch(cmd.op){
            case HostOp::ScheduleShipment:
                if(cmd.carrier_receipt.empty())
                    throw std::invalid_argument("missing physically validated carrier receipt");
                return scheduler.schedule_shipment(cmd.origin,cmd.destination,cmd.cargo,
                                                   cmd.at_tick,cmd.travel_ticks);
            case HostOp::ScheduleHabitatDelivery:
                if(cmd.carrier_receipt.empty())
                    throw std::invalid_argument("missing verified local worker receipt");
                return scheduler.schedule_habitat_delivery(cmd.destination,cmd.cargo,cmd.at_tick);
            case HostOp::SchedulePopulation:
                if(cmd.carrier_receipt.empty())
                    throw std::invalid_argument("missing host-verified demographic event receipt");
                scheduler.schedule_population(cmd.destination,cmd.population,cmd.at_tick);
                return 0;
            case HostOp::Advance:
                scheduler.advance_to(cmd.at_tick);
                return 0;
            case HostOp::AcknowledgeNotices: {
                // Only ack a specific head entry already durably consumed by the
                // host. A count would silently drop unrelated queued messages.
                const auto& pending=scheduler.pending_notices();
                if(cmd.expected_notice_sequence==0 || pending.empty() ||
                   pending.front().sequence!=cmd.expected_notice_sequence)
                    throw std::invalid_argument("regional notice ack sequence mismatch");
                return scheduler.take_notices(1).front().sequence;
            }
            }
            throw std::invalid_argument("unknown regional host operation");
        },custody_ ? SqliteMutationEffect([this,&cmd](sqlite3* db,const Scheduler& before,
                  const Scheduler& after,std::uint64_t outcome){
                    custody_->record(db,cmd,before,after,outcome);
                  }) : SqliteMutationEffect{},custody_!=nullptr,
                  expected_owner_generation);
}
} // namespace galaxy::regional_events
