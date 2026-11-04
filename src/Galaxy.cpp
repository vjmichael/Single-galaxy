#include "galaxy/Galaxy.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace galaxy {
namespace {
std::pair<std::uint32_t,std::uint32_t> edge(std::uint32_t a,std::uint32_t b) {
    return a < b ? std::make_pair(a,b) : std::make_pair(b,a);
}
bool bounded(Vector2 p) {
    return p.x >= -Simulation::kWorldBoundMm && p.x <= Simulation::kWorldBoundMm &&
           p.y >= -Simulation::kWorldBoundMm && p.y <= Simulation::kWorldBoundMm;
}
void sql_check(int rc, sqlite3* db, const char* what) {
    if(rc!=SQLITE_OK && rc!=SQLITE_DONE && rc!=SQLITE_ROW)
        throw std::runtime_error(std::string(what)+": "+sqlite3_errmsg(db));
}
class Stmt {
public:
    Stmt(sqlite3* db,const char* sql):db_(db) { sql_check(sqlite3_prepare_v2(db,sql,-1,&st_,nullptr),db,"prepare"); }
    ~Stmt(){ sqlite3_finalize(st_); }
    Stmt(const Stmt&)=delete; Stmt& operator=(const Stmt&)=delete;
    sqlite3_stmt* get() const {return st_;}
    int step() { int rc=sqlite3_step(st_); sql_check(rc,db_,"step");return rc; }
private:sqlite3* db_;sqlite3_stmt* st_=nullptr;
};
} // namespace
void GalaxyMap::add_system(SystemDescriptor s) {
    if(s.id==0 || s.name.empty() || s.name.size()>100 || s.security<0 || s.security>100 ||
       !systems_.emplace(s.id,std::move(s)).second) throw std::invalid_argument("invalid or duplicate system");
}
void GalaxyMap::link(std::uint32_t a,std::uint32_t b,bool operational) {
    if(a==b || !systems_.count(a) || !systems_.count(b) || !links_.emplace(edge(a,b),operational).second)
        throw std::invalid_argument("invalid or duplicate gate link");
}
void GalaxyMap::set_link_operational(std::uint32_t a,std::uint32_t b,bool operational) {
    auto i=links_.find(edge(a,b));if(i==links_.end())throw std::invalid_argument("unknown gate link");
    i->second=operational;
}
void GalaxyMap::add_site(std::uint32_t system,SpaceSite site) {
    if(!systems_.count(system)||site.id==0||site.label.size()>120||!bounded(site.position)||
       site.probe_difficulty<0||site.probe_difficulty>100)throw std::invalid_argument("invalid site");
    auto& v=sites_[system];
    if(std::any_of(v.begin(),v.end(),[&](const SpaceSite& x){return x.id==site.id;}))
        throw std::invalid_argument("duplicate site id");
    v.push_back(std::move(site));
    std::sort(v.begin(),v.end(),[](const SpaceSite& a,const SpaceSite& b){return a.id<b.id;});
}
const SystemDescriptor& GalaxyMap::system(std::uint32_t id) const {return systems_.at(id);}
const std::vector<SpaceSite>& GalaxyMap::sites(std::uint32_t system) const {
    (void)this->system(system);
    static const std::vector<SpaceSite> empty;
    auto i=sites_.find(system);return i==sites_.end()?empty:i->second;
}
std::vector<SpaceSite> GalaxyMap::visible_on_entry(std::uint32_t system) const {
    std::vector<SpaceSite> v;for(const auto& site:sites(system))if(site.mapped_on_entry)v.push_back(site);
    return v;
}
RouteResult GalaxyMap::route(std::uint32_t from,std::uint32_t to,RoutePolicy policy) const {
    (void)system(from);(void)system(to);
    // Dijkstra, deterministic ties by system ID. PreferSecure is a penalty,
    // not a false assertion that a dangerous route is physically inaccessible.
    using Q=std::pair<std::uint64_t,std::uint32_t>;
    std::priority_queue<Q,std::vector<Q>,std::greater<Q>> queue;
    std::map<std::uint32_t,std::uint64_t> best;
    std::map<std::uint32_t,std::uint32_t> prev;
    best[from]=0;queue.push({0,from});
    while(!queue.empty()) {
        const auto [cost,u]=queue.top();queue.pop();
        if(cost!=best[u])continue;
        for(const auto& [link,operational]:links_) {
            if(!operational || (link.first!=u && link.second!=u))continue;
            const auto v=link.first==u?link.second:link.first;
            const auto penalty=policy==RoutePolicy::PreferSecure ?
                std::uint64_t(100-system(v).security)*4ULL:0ULL;
            const auto candidate=cost+100+penalty;
            if(!best.count(v) || candidate<best[v] ||
               (candidate==best[v] && u<prev[v])) {
                best[v]=candidate;prev[v]=u;queue.push({candidate,v});
            }
        }
    }
    if(!best.count(to))return {};
    RouteResult route;route.cost=best[to];
    for(auto x=to;;x=prev.at(x)) {
        route.systems.push_back(x);if(x==from)break;
    }
    std::reverse(route.systems.begin(),route.systems.end());return route;
}
AstronomicalProfile GalaxyMap::astronomy(std::uint32_t system,
    std::uint64_t galaxy_seed) const {
    (void)this->system(system);
    return generate_astronomical_profile(galaxy_seed,system);
}
std::vector<DirectionalContact> SurveySensors::directional(std::uint32_t system,Vector2 ship,
    double heading_deg,double cone_deg,std::int64_t range_mm) const {
    if(!bounded(ship)||!std::isfinite(heading_deg)||!std::isfinite(cone_deg)||
       cone_deg<=0||cone_deg>360||range_mm<=0||range_mm>Simulation::kWorldBoundMm*2)
        throw std::invalid_argument("invalid directional scan");
    std::vector<DirectionalContact> found;
    constexpr double pi=3.14159265358979323846;
    for(const auto& site:map_.sites(system)) {
        if(!site.directional_visible)continue;
        const double dx=double(site.position.x)-double(ship.x);
        const double dy=double(site.position.y)-double(ship.y);
        const double dist=std::hypot(dx,dy);
        if(dist>double(range_mm))continue;
        const auto bearing=std::atan2(dy,dx)*180.0/pi;
        auto relative=std::remainder(bearing-heading_deg,360.0);
        if(cone_deg<360 && std::abs(relative)>cone_deg/2.0+1e-8)continue;
        auto deg=std::lround(std::fmod(bearing+360.0,360.0));
        found.push_back({site.id,site.kind,int(deg%360),std::int64_t(std::llround(dist/1000.0))*1000});
    }
    return found;
}
void SurveySensors::register_probe(std::uint64_t id,PlayerId owner) {
    if(!id||!owner||!probes_.emplace(id,Probe{id,owner,0,{},false}).second)
        throw std::invalid_argument("invalid or duplicate probe");
}
bool SurveySensors::deploy(std::uint64_t id,PlayerId owner,std::uint32_t system,Vector2 p) {
    (void)map_.system(system);
    auto i=probes_.find(id);
    if(i==probes_.end()||i->second.owner!=owner||i->second.deployed||!bounded(p))return false;
    i->second={id,owner,system,p,true};return true;
}
bool SurveySensors::recover(std::uint64_t id,PlayerId owner,std::uint32_t system) {
    auto i=probes_.find(id);
    if(i==probes_.end()||i->second.owner!=owner||!i->second.deployed||i->second.system!=system)return false;
    i->second.deployed=false;i->second.system=0;return true;
}
std::size_t SurveySensors::deployed_count(PlayerId owner) const {
    std::size_t n=0;for(const auto& [id,p]:probes_) { (void)id;if(p.owner==owner&&p.deployed)++n; }return n;
}
std::vector<SignatureContact> SurveySensors::coarse_signatures(PlayerId owner,
    std::uint32_t system,Vector2 scanner_position,std::int64_t range_mm,
    int sensor_strength) const {
    if(!owner||!bounded(scanner_position)||range_mm<=0||
       range_mm>Simulation::kWorldBoundMm*2||sensor_strength<1||sensor_strength>100)
        throw std::invalid_argument("invalid signature search");
    std::vector<SignatureContact> results;
    for(const auto& site:map_.sites(system)) {
        // Only sufficiently detectable hidden signatures. Do not expose kind
        // or precise coordinates until real samples support classification.
        if(site.mapped_on_entry||site.probe_difficulty==0||
           sensor_strength<(site.probe_difficulty+1)/2)continue;
        const double distance=std::hypot(double(site.position.x)-scanner_position.x,
                                         double(site.position.y)-scanner_position.y);
        if(distance>double(range_mm))continue;
        constexpr std::int64_t quant=5000;
        const auto coarse=[](std::int64_t x) {
            return std::int64_t(std::llround(double(x)/5000.0))*5000;
        };
        (void)quant;
        results.push_back({site.id,{coarse(site.position.x),coarse(site.position.y)},5000});
    }
    return results;
}
std::optional<RefinementPlan> SurveySensors::plan_refinement(PlayerId owner,
    std::uint32_t system,std::uint64_t signature,Vector2 scanner_position,
    std::int64_t discovery_range_mm,std::int64_t scan_radius_mm,
    int sensor_strength) const {
    if(!owner||!signature||!bounded(scanner_position)||discovery_range_mm<=0||
       discovery_range_mm>Simulation::kWorldBoundMm*2||scan_radius_mm<=0||
       scan_radius_mm>Simulation::kWorldBoundMm*2||
       sensor_strength<1||sensor_strength>100)
        throw std::invalid_argument("invalid signature refinement request");
    // A lock requires the actual target to be detectable from the trusted
    // host's current scanner location and configured range. This plan is for the trusted host, never a client map.
    const auto& sites=map_.sites(system);
    const auto target=std::find_if(sites.begin(),sites.end(),
        [signature](const SpaceSite& s){return s.id==signature;});
    if(target==sites.end()||target->mapped_on_entry||target->probe_difficulty==0||
       sensor_strength<(target->probe_difficulty+1)/2||
       std::hypot(double(target->position.x)-scanner_position.x,
                  double(target->position.y)-scanner_position.y)>double(discovery_range_mm))
        return std::nullopt;
    std::vector<std::uint64_t> owned;
    for(const auto& [id,p]:probes_)
        if(p.owner==owner&&p.deployed&&p.system==system)owned.push_back(id);
    if(owned.empty())return std::nullopt;
    // Use the coarse result, not the exact hidden target. The host moves
    // physical probes to these waypoints over committed simulation ticks.
    const auto quant=[](std::int64_t x) {
        return std::int64_t(std::llround(double(x)/5000.0))*5000;
    };
    const Vector2 center{quant(target->position.x),quant(target->position.y)};
    const auto spread=std::min<std::int64_t>(2500,std::max<std::int64_t>(250,scan_radius_mm/2));
    RefinementPlan plan;plan.site=signature;
    constexpr double pi=3.14159265358979323846;
    for(std::size_t i=0;i<owned.size();++i) {
        const double angle=2*pi*double(i)/double(owned.size());
        Vector2 point{center.x+std::int64_t(std::llround(spread*std::cos(angle))),
                      center.y+std::int64_t(std::llround(spread*std::sin(angle)))};
        point.x=std::clamp(point.x,-Simulation::kWorldBoundMm,Simulation::kWorldBoundMm);
        point.y=std::clamp(point.y,-Simulation::kWorldBoundMm,Simulation::kWorldBoundMm);
        plan.waypoints.emplace_back(owned[i],point);
    }
    return plan;
}
bool SurveySensors::advance_refinement(PlayerId owner,std::uint32_t system,
    std::uint64_t signature,Vector2 scanner_position,
    std::int64_t discovery_range_mm,std::int64_t scan_radius_mm,
    int sensor_strength,std::int64_t max_step_mm) {
    if(max_step_mm<1||max_step_mm>Simulation::kWorldBoundMm)
        throw std::invalid_argument("invalid physical probe step");
    const auto plan=plan_refinement(owner,system,signature,scanner_position,
        discovery_range_mm,scan_radius_mm,sensor_strength);
    if(!plan)return false;
    bool complete=true;
    for(const auto& [id,waypoint]:plan->waypoints) {
        auto p=probes_.find(id);
        if(p==probes_.end()||!p->second.deployed||
           p->second.owner!=owner||p->second.system!=system)
            throw std::logic_error("probe ownership changed during refinement");
        auto& position=p->second.position;
        const double dx=double(waypoint.x)-position.x;
        const double dy=double(waypoint.y)-position.y;
        const double length=std::hypot(dx,dy);
        if(length<=double(max_step_mm)) {
            position=waypoint;
        } else {
            // No teleportation: movement never exceeds a single scheduled
            // host-tick step. Minimum one millimetre of progress.
            auto mx=std::int64_t(std::llround(double(max_step_mm)*dx/length));
            auto my=std::int64_t(std::llround(double(max_step_mm)*dy/length));
            if(mx==0&&my==0) {
                if(std::abs(dx)>=std::abs(dy))mx=dx>0?1:-1;
                else my=dy>0?1:-1;
            }
            position.x+=mx;
            position.y+=my;
            complete=false;
        }
    }
    return complete;
}
std::vector<ScanObservation> SurveySensors::probe_scan(PlayerId owner,std::uint32_t system,
    std::uint64_t tick,std::int64_t radius_mm,int sensor_strength) const {
    if(!owner||!tick||radius_mm<=0||radius_mm>Simulation::kWorldBoundMm*2||
       sensor_strength<1||sensor_strength>100)throw std::invalid_argument("invalid probe scan");
    std::vector<ScanObservation> found;
    for(const auto& site:map_.sites(system)) {
        if(site.probe_difficulty==0 || site.mapped_on_entry || site.probe_difficulty>sensor_strength)continue;
        std::vector<Vector2> independent_positions;
        for(const auto& [id,p]:probes_) {
            (void)id;
            if(p.owner!=owner||!p.deployed||p.system!=system)continue;
            const double dist=std::hypot(double(site.position.x)-p.position.x,
                                         double(site.position.y)-p.position.y);
            if(dist>double(radius_mm))continue;
            bool distinct=true;
            for(const auto& previous:independent_positions) {
                const auto separation=std::hypot(double(p.position.x)-previous.x,
                                                 double(p.position.y)-previous.y);
                if(separation<250.0) {distinct=false;break;}
            }
            if(distinct)independent_positions.push_back(p.position);
        }
        if(independent_positions.empty())continue;
        const int count=int(independent_positions.size());
        // Weak single-probe observations are coarse candidate locations, not
        // perfect coordinates. Multiple separated probes improve resolution.
        const int confidence=std::min(100,35+count*20+std::max(0,sensor_strength-site.probe_difficulty)/2);
        auto estimate=site.position;
        if(confidence<75) {
            estimate.x=std::int64_t(std::llround(double(estimate.x)/2500.0))*2500;
            estimate.y=std::int64_t(std::llround(double(estimate.y)/2500.0))*2500;
        }
        found.push_back({owner,system,site.id,site.kind,estimate,tick,confidence,false});
    }
    return found;
}
std::vector<ScanObservation> SurveySensors::scan_mode(PlayerId owner,std::uint32_t system,
    std::uint64_t tick,std::int64_t radius_mm,int sensor_strength,ScanMode mode) const {
    // Classification is evaluated on genuine native probe readings. This
    // prototype cannot conjure hidden information from client-selected pixels.
    const auto observations=probe_scan(owner,system,tick,radius_mm,sensor_strength);
    std::vector<ScanObservation> selected;
    for(const auto& o:observations) {
        const bool environmental=o.kind==SiteKind::GasCloud;
        const bool celestial=o.kind==SiteKind::Star||o.kind==SiteKind::Planet||
            o.kind==SiteKind::Moon||o.kind==SiteKind::AsteroidDeposit;
        const bool anomaly=o.kind==SiteKind::Anomaly||o.kind==SiteKind::Wormhole||
            o.kind==SiteKind::Structure;
        if((mode==ScanMode::Environmental&&environmental)||
           (mode==ScanMode::Celestial&&celestial)||
           (mode==ScanMode::Anomaly&&anomaly))selected.push_back(o);
    }
    return selected;
}
SurveyArchive::SurveyArchive(const std::string& path) {
    if(sqlite3_open_v2(path.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
        std::string err=db_?sqlite3_errmsg(db_):"opening SQLite failed";
        if(db_)sqlite3_close(db_); db_=nullptr;throw std::runtime_error(err);
    }
    try {
        sql_check(sqlite3_exec(db_,"PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;"
            "CREATE TABLE IF NOT EXISTS observations("
            "player INTEGER NOT NULL CHECK(player>0),system INTEGER NOT NULL CHECK(system>0),"
            "site INTEGER NOT NULL CHECK(site>0),kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 9),"
            "x INTEGER NOT NULL,y INTEGER NOT NULL,tick INTEGER NOT NULL CHECK(tick>0),"
            "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 1 AND 100),"
            "published INTEGER NOT NULL CHECK(published IN (0,1)),"
            "PRIMARY KEY(player,system,site));"
            "CREATE TABLE IF NOT EXISTS observation_history("
            "revision INTEGER PRIMARY KEY AUTOINCREMENT,"
            "player INTEGER NOT NULL CHECK(player>0), system INTEGER NOT NULL CHECK(system>0),"
            "site INTEGER NOT NULL CHECK(site>0),kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 9),"
            "x INTEGER NOT NULL,y INTEGER NOT NULL,tick INTEGER NOT NULL CHECK(tick>0),"
            "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 1 AND 100),"
            "published INTEGER NOT NULL CHECK(published IN (0,1)));"
            "CREATE INDEX IF NOT EXISTS idx_survey_history_owner ON observation_history(player,system,revision DESC);"
            "CREATE TABLE IF NOT EXISTS pending_probe_reports("
            "player INTEGER NOT NULL CHECK(player>0),system INTEGER NOT NULL CHECK(system>0),"
            "site INTEGER NOT NULL CHECK(site>0),kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 9),"
            "x INTEGER NOT NULL,y INTEGER NOT NULL,tick INTEGER NOT NULL CHECK(tick>0),"
            "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 1 AND 100),"
            "captured_ms INTEGER NOT NULL CHECK(captured_ms>=0),"
            "PRIMARY KEY(player,system,site,tick));"
            "CREATE INDEX IF NOT EXISTS idx_probe_pending_ready ON pending_probe_reports(player,captured_ms);",
            nullptr,nullptr,nullptr),db_,"create survey archive");
    } catch(...) {sqlite3_close(db_);db_=nullptr;throw;}
}
SurveyArchive::~SurveyArchive(){if(db_)sqlite3_close(db_);}
bool SurveyArchive::record(const ScanObservation& o) {
    if(!o.player||!o.system||!o.site||!o.observed_tick ||
       o.player>std::uint64_t(std::numeric_limits<std::int64_t>::max()) ||
       o.site>std::uint64_t(std::numeric_limits<std::int64_t>::max()) ||
       o.observed_tick>std::uint64_t(std::numeric_limits<std::int64_t>::max()) ||
       o.confidence<1||o.confidence>100||!bounded(o.estimated_position)||int(o.kind)<0||int(o.kind)>9)
        throw std::invalid_argument("invalid survey record");
    sql_check(sqlite3_exec(db_,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr),db_,"survey begin");
    try {
    Stmt stmt(db_,"INSERT INTO observations(player,system,site,kind,x,y,tick,confidence,published) "
        "VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(player,system,site) DO UPDATE SET "
        "kind=excluded.kind,x=excluded.x,y=excluded.y,tick=excluded.tick,confidence=excluded.confidence,"
        "published=MAX(observations.published,excluded.published) "
        "WHERE excluded.tick>observations.tick OR (excluded.tick=observations.tick AND excluded.published>observations.published)");
    sqlite3_bind_int64(stmt.get(),1,sqlite3_int64(o.player));sqlite3_bind_int64(stmt.get(),2,o.system);
    sqlite3_bind_int64(stmt.get(),3,sqlite3_int64(o.site));sqlite3_bind_int(stmt.get(),4,int(o.kind));
    sqlite3_bind_int64(stmt.get(),5,o.estimated_position.x);sqlite3_bind_int64(stmt.get(),6,o.estimated_position.y);
    sqlite3_bind_int64(stmt.get(),7,sqlite3_int64(o.observed_tick));sqlite3_bind_int(stmt.get(),8,o.confidence);
    sqlite3_bind_int(stmt.get(),9,o.published?1:0);stmt.step();
    const bool changed=sqlite3_changes(db_)>0;
    if(changed) {
        // The snapshot can remain public after a subsequent private scan.
        // The revision log must record the EFFECTIVE committed visibility,
        // not blindly copy the request's published flag.
        Stmt visibility_stmt(db_,"SELECT published FROM observations WHERE player=? AND system=? AND site=?");
        sqlite3_bind_int64(visibility_stmt.get(),1,sqlite3_int64(o.player));
        sqlite3_bind_int64(visibility_stmt.get(),2,o.system);
        sqlite3_bind_int64(visibility_stmt.get(),3,sqlite3_int64(o.site));
        if(visibility_stmt.step()!=SQLITE_ROW)throw std::runtime_error("survey snapshot vanished");
        const int visible=sqlite3_column_int(visibility_stmt.get(),0);
        Stmt history_stmt(db_,"INSERT INTO observation_history(player,system,site,kind,x,y,tick,confidence,published) "
            "VALUES(?,?,?,?,?,?,?,?,?)");
        sqlite3_bind_int64(history_stmt.get(),1,sqlite3_int64(o.player));
        sqlite3_bind_int64(history_stmt.get(),2,o.system);
        sqlite3_bind_int64(history_stmt.get(),3,sqlite3_int64(o.site));
        sqlite3_bind_int(history_stmt.get(),4,int(o.kind));
        sqlite3_bind_int64(history_stmt.get(),5,o.estimated_position.x);
        sqlite3_bind_int64(history_stmt.get(),6,o.estimated_position.y);
        sqlite3_bind_int64(history_stmt.get(),7,sqlite3_int64(o.observed_tick));
        sqlite3_bind_int(history_stmt.get(),8,o.confidence);
        sqlite3_bind_int(history_stmt.get(),9,visible);
        history_stmt.step();
    }
    sql_check(sqlite3_exec(db_,"COMMIT",nullptr,nullptr,nullptr),db_,"survey commit");
    return changed;
    }catch(...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
std::vector<ScanObservation> SurveyArchive::history(PlayerId owner,std::uint32_t system,std::size_t limit) const {
    if(!owner||!system||limit<1||limit>100)throw std::invalid_argument("invalid survey history selector");
    Stmt stmt(db_,"SELECT site,kind,x,y,tick,confidence,published FROM observation_history "
        "WHERE player=? AND system=? ORDER BY revision DESC LIMIT ?");
    sqlite3_bind_int64(stmt.get(),1,sqlite3_int64(owner));
    sqlite3_bind_int64(stmt.get(),2,system);
    sqlite3_bind_int(stmt.get(),3,int(limit));
    std::vector<ScanObservation> result;
    while(stmt.step()==SQLITE_ROW)result.push_back({owner,system,
        std::uint64_t(sqlite3_column_int64(stmt.get(),0)),SiteKind(sqlite3_column_int(stmt.get(),1)),
        {sqlite3_column_int64(stmt.get(),2),sqlite3_column_int64(stmt.get(),3)},
        std::uint64_t(sqlite3_column_int64(stmt.get(),4)),sqlite3_column_int(stmt.get(),5),sqlite3_column_int(stmt.get(),6)!=0});
    return result;
}
std::vector<ScanObservation> SurveyArchive::personal(PlayerId owner,std::uint32_t system) const {
    if(!owner||!system)throw std::invalid_argument("invalid survey selector");
    Stmt stmt(db_,"SELECT site,kind,x,y,tick,confidence,published FROM observations "
        "WHERE player=? AND system=? ORDER BY site");
    sqlite3_bind_int64(stmt.get(),1,sqlite3_int64(owner));sqlite3_bind_int64(stmt.get(),2,system);
    std::vector<ScanObservation> result;
    while(stmt.step()==SQLITE_ROW)result.push_back({owner,system,
        std::uint64_t(sqlite3_column_int64(stmt.get(),0)),SiteKind(sqlite3_column_int(stmt.get(),1)),
        {sqlite3_column_int64(stmt.get(),2),sqlite3_column_int64(stmt.get(),3)},
        std::uint64_t(sqlite3_column_int64(stmt.get(),4)),sqlite3_column_int(stmt.get(),5),sqlite3_column_int(stmt.get(),6)!=0});
    return result;
}
std::vector<SurveyArchive::PublicRecord> SurveyArchive::published(std::uint32_t system) const {
    if(!system)throw std::invalid_argument("invalid system selector");
    Stmt stmt(db_,"SELECT site,MIN(kind),COUNT(*),AVG(confidence),MAX(tick),AVG(x),AVG(y) FROM observations "
        "WHERE system=? AND published=1 GROUP BY site ORDER BY site");
    sqlite3_bind_int(stmt.get(),1,system);
    std::vector<PublicRecord> out;
    while(stmt.step()==SQLITE_ROW) {
        int independent=sqlite3_column_int(stmt.get(),2);
        // A single contributor is never treated as multiple independent samples.
        int confidence=std::min(99,sqlite3_column_int(stmt.get(),3)+
            std::min(20,(independent-1)*5));
        out.push_back({system,std::uint64_t(sqlite3_column_int64(stmt.get(),0)),
            SiteKind(sqlite3_column_int(stmt.get(),1)),independent,confidence,
            std::uint64_t(sqlite3_column_int64(stmt.get(),4)),
            {sqlite3_column_int64(stmt.get(),5),sqlite3_column_int64(stmt.get(),6)}});
    }
    return out;
}
std::size_t SurveyArchive::import_native_discoveries(const Simulation& world,
    PlayerId owner,std::uint32_t system) {
    if(!owner||!system)throw std::invalid_argument("invalid native discovery import");
    std::size_t imported=0;
    for(const auto& [key,d]:world.discoveries()) {
        (void)key;
        if(d.owner!=owner||d.system!=system)continue;
        // Native v67 journal stores verified asteroid discoveries, including
        // non-extractive geological classifications. Do not import unknown
        // entity kinds as fabricated settlement sites.
        const Entity* source=world.find(d.site);
        if(source && source->type!=EntityType::Asteroid)continue;
        if(!bounded(d.location)||d.first_tick==0)continue;
        const int confidence=d.survey_taxonomy?100:(d.geological?85:70);
        ScanObservation observation{owner,system,d.site,SiteKind::AsteroidDeposit,
            d.location,d.first_tick,confidence,false};
        if(record(observation))++imported;
    }
    return imported;
}
std::optional<ScanObservation> SurveyArchive::known_site(PlayerId owner,
    std::uint32_t system,std::uint64_t site) const {
    if(!owner||!system||!site||
       owner>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       site>std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("invalid known-site selector");
    Stmt stmt(db_,"SELECT kind,x,y,tick,confidence,published FROM observations "
        "WHERE player=? AND system=? AND site=?");
    sqlite3_bind_int64(stmt.get(),1,sqlite3_int64(owner));
    sqlite3_bind_int(stmt.get(),2,int(system));
    sqlite3_bind_int64(stmt.get(),3,sqlite3_int64(site));
    if(stmt.step()!=SQLITE_ROW)return std::nullopt;
    return ScanObservation{owner,system,site,SiteKind(sqlite3_column_int(stmt.get(),0)),
        {sqlite3_column_int64(stmt.get(),1),sqlite3_column_int64(stmt.get(),2)},
        std::uint64_t(sqlite3_column_int64(stmt.get(),3)),
        sqlite3_column_int(stmt.get(),4),sqlite3_column_int(stmt.get(),5)!=0};
}
std::vector<ScanObservation> SurveyArchive::known_sites(PlayerId owner,
    std::uint32_t system,int minimum_confidence) const {
    if(!owner||!system||owner>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       minimum_confidence<1||minimum_confidence>100)
        throw std::invalid_argument("invalid known-sites selector");
    Stmt stmt(db_,"SELECT site,kind,x,y,tick,confidence,published FROM observations "
        "WHERE player=? AND system=? AND confidence>=? ORDER BY site");
    sqlite3_bind_int64(stmt.get(),1,sqlite3_int64(owner));
    sqlite3_bind_int(stmt.get(),2,int(system));
    sqlite3_bind_int(stmt.get(),3,minimum_confidence);
    std::vector<ScanObservation> result;
    while(stmt.step()==SQLITE_ROW)
        result.push_back({owner,system,std::uint64_t(sqlite3_column_int64(stmt.get(),0)),
            SiteKind(sqlite3_column_int(stmt.get(),1)),
            {sqlite3_column_int64(stmt.get(),2),sqlite3_column_int64(stmt.get(),3)},
            std::uint64_t(sqlite3_column_int64(stmt.get(),4)),
            sqlite3_column_int(stmt.get(),5),sqlite3_column_int(stmt.get(),6)!=0});
    return result;
}

bool SurveyArchive::queue_probe_report(const ScanObservation& o,
    std::uint64_t captured_at_ms) {
    if(!o.player||!o.system||!o.site||!o.observed_tick||
       o.player>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       o.site>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       o.observed_tick>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       captured_at_ms>std::uint64_t(std::numeric_limits<std::int64_t>::max())-kNominalResponseMs||
       o.confidence<1||o.confidence>100||!bounded(o.estimated_position)||
       int(o.kind)<0||int(o.kind)>9||o.published)
        throw std::invalid_argument("invalid trusted probe report");
    Stmt stmt(db_,"INSERT OR IGNORE INTO pending_probe_reports"
        "(player,system,site,kind,x,y,tick,confidence,captured_ms) VALUES(?,?,?,?,?,?,?,?,?)");
    sqlite3_bind_int64(stmt.get(),1,sqlite3_int64(o.player));
    sqlite3_bind_int(stmt.get(),2,int(o.system));
    sqlite3_bind_int64(stmt.get(),3,sqlite3_int64(o.site));
    sqlite3_bind_int(stmt.get(),4,int(o.kind));
    sqlite3_bind_int64(stmt.get(),5,o.estimated_position.x);
    sqlite3_bind_int64(stmt.get(),6,o.estimated_position.y);
    sqlite3_bind_int64(stmt.get(),7,sqlite3_int64(o.observed_tick));
    sqlite3_bind_int(stmt.get(),8,o.confidence);
    sqlite3_bind_int64(stmt.get(),9,sqlite3_int64(captured_at_ms));
    stmt.step();
    return sqlite3_changes(db_)>0;
}
std::size_t SurveyArchive::deliver_probe_reports(PlayerId owner,
    std::uint64_t now_ms,std::int64_t link_distance_mm) {
    if(!owner||owner>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       now_ms>std::uint64_t(std::numeric_limits<std::int64_t>::max())||
       link_distance_mm<0)throw std::invalid_argument("invalid trusted probe delivery");
    if(link_distance_mm>kDeepProbeLinkMm||now_ms<kNominalResponseMs)return 0;
    // Read pending rows before calling record(), which uses its own transaction.
    // Crash between record and delete safely replays an idempotent revision.
    Stmt stmt(db_,"SELECT system,site,kind,x,y,tick,confidence FROM pending_probe_reports "
        "WHERE player=? AND captured_ms<=? ORDER BY captured_ms,site,tick");
    sqlite3_bind_int64(stmt.get(),1,sqlite3_int64(owner));
    sqlite3_bind_int64(stmt.get(),2,sqlite3_int64(now_ms-kNominalResponseMs));
    std::vector<ScanObservation> reports;
    while(stmt.step()==SQLITE_ROW)
        reports.push_back({owner,std::uint32_t(sqlite3_column_int(stmt.get(),0)),
            std::uint64_t(sqlite3_column_int64(stmt.get(),1)),
            SiteKind(sqlite3_column_int(stmt.get(),2)),
            {sqlite3_column_int64(stmt.get(),3),sqlite3_column_int64(stmt.get(),4)},
            std::uint64_t(sqlite3_column_int64(stmt.get(),5)),
            sqlite3_column_int(stmt.get(),6),false});
    std::size_t delivered=0;
    for(const auto& report:reports) {
        record(report);
        Stmt clear(db_,"DELETE FROM pending_probe_reports WHERE player=? AND system=? AND site=? AND tick=?");
        sqlite3_bind_int64(clear.get(),1,sqlite3_int64(owner));
        sqlite3_bind_int(clear.get(),2,int(report.system));
        sqlite3_bind_int64(clear.get(),3,sqlite3_int64(report.site));
        sqlite3_bind_int64(clear.get(),4,sqlite3_int64(report.observed_tick));
        clear.step();
        ++delivered;
    }
    return delivered;
}

std::vector<ScanObservation> SurveyArchive::actionable_sites(PlayerId owner,
    std::uint32_t system,const GalaxyMap& live_map,DiscoveryUse purpose,
    int minimum_confidence,std::uint64_t current_tick,
    std::uint64_t max_age_ticks) const {
    // Advisory-only: live game actions must independently verify a destination,
    // resource quantity, permits, geometry and player authorization.
    const auto candidates=known_sites(owner,system,minimum_confidence);
    const auto& live=live_map.sites(system);
    std::vector<ScanObservation> result;
    for(const auto& candidate:candidates) {
        if(candidate.observed_tick>current_tick||
           current_tick-candidate.observed_tick>max_age_ticks)continue;
        const auto site=std::find_if(live.begin(),live.end(),
            [&](const SpaceSite& s){return s.id==candidate.site;});
        if(site==live.end()||site->kind!=candidate.kind)continue;
        const bool navigate=(site->kind==SiteKind::Planet||
            site->kind==SiteKind::Moon||site->kind==SiteKind::Station||
            site->kind==SiteKind::Structure);
        const bool settle=(site->kind==SiteKind::Planet||
            site->kind==SiteKind::Moon||site->kind==SiteKind::AsteroidDeposit);
        if((purpose==DiscoveryUse::Navigation&&navigate)||
           (purpose==DiscoveryUse::SettlementSurvey&&settle))
            result.push_back(candidate);
    }
    return result;
}

} // namespace galaxy
