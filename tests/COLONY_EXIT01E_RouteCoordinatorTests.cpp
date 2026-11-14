#include "galaxy/VoluntaryRouteCoordinator.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy::colony_routes;
#define CHECK(x) do{if(!(x))throw std::runtime_error("route test line "+std::to_string(__LINE__)+": " #x);}while(false)
int main(){
    try {
        Coordinator c;
        Request request{11,7,50,100,1};
        Offer good{11,21,31,10,7,50,104,501,601,701,801,4,2};
        Offer alternate=good;
        alternate.destination=22;alternate.ferry=32;
        alternate.admission_id=502;alternate.transport_id=602;
        alternate.boarding_id=702;alternate.pressure_id=802;
        int auth=0,physical=0,queued=0;
        auto verified=[&](const Offer& o){++auth;return o.receiving_owner==10;};
        auto preflight=[&](const Offer& o){++physical;return o.ferry!=0;};
        auto dispatch=[&](const Offer&){++queued;return true;};
        CHECK(c.coordinate(request,{},verified,preflight,dispatch).status==Status::NoOffers);
        auto malformed=good;malformed.receiving_owner=7;
        CHECK(c.coordinate(request,{malformed},verified,preflight,dispatch).status==Status::NoAuthenticatedRoute);
        CHECK(auth==0 && physical==0 && queued==0);
        auto stale=good;stale.generation=49;
        CHECK(c.coordinate(request,{stale},verified,preflight,dispatch).status==Status::NoAuthenticatedRoute);
        stale=good;stale.expiry_tick=99;
        CHECK(c.coordinate(request,{stale},verified,preflight,dispatch).status==Status::NoAuthenticatedRoute);
        auto full=good;full.occupied_seats=4;
        CHECK(c.coordinate(request,{full},verified,preflight,dispatch).status==Status::NoCapacity);
        CHECK(queued==0);
        const auto result=c.coordinate(request,{alternate,good},verified,preflight,dispatch);
        CHECK(result.status==Status::Dispatched && result.destination==21 && result.ferry==31);
        CHECK(result.admission_id==501 && queued==1);
        // Authentication failure never becomes a physical booking.
        const auto deny=[](const Offer&){return false;};
        CHECK(c.coordinate(request,{good},deny,preflight,dispatch).status==Status::NoAuthenticatedRoute);
        CHECK(queued==1);
        // A failed native transaction does not claim success; next call may
        // retry unchanged immutable admission, leaving idempotence to SQLite.
        CHECK(c.coordinate(request,{good},verified,preflight,deny).status!=Status::Dispatched);
        CHECK(c.coordinate(request,{good},verified,preflight,dispatch).status==Status::Dispatched);
        // A returned approval is tied to the exact route: do not silently
        // apply the same admission/boarding IDs to a different vessel.
        auto duplicate=good;duplicate.ferry=99;
        int attempts=0;
        const auto count=[&](const Offer&){++attempts;return true;};
        auto duplicateResult=c.coordinate(request,{good,duplicate},verified,preflight,count);
        CHECK(duplicateResult.status==Status::Dispatched && attempts==1);
        bool refused=false;
        try { auto tooMany=std::vector<Offer>(65,good);
            (void)c.coordinate(request,tooMany,verified,preflight,dispatch);
        } catch(const std::invalid_argument&){refused=true;}
        CHECK(refused);
        std::cout<<"PASS EXIT01E.A: exact owner and generation, bounded offers, real broker veto, physical veto, capacity, deterministic selection, no false bookings\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
