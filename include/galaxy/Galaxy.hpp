#pragma once
// B5.4/B6.0: standalone geography/navigation and sensor discovery contracts.
// The authoritative simulation remains Simulation/PersistentWorld; no global
// omniscient object stream is exposed to a player by this API.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <sqlite3.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace galaxy {
enum class Sovereignty { NPC, Player, Unclaimed };
enum class SiteKind { Star, Planet, Moon, Station, Ship, AsteroidDeposit, GasCloud, Anomaly, Wormhole, Structure };
enum class RoutePolicy { Shortest, PreferSecure };
struct SystemDescriptor {
    std::uint32_t id = 0;
    std::string name;
    int security = 0; // 0..100, 0 unpoliced, 100 safest; separate from sovereignty
    Sovereignty sovereignty = Sovereignty::Unclaimed;
};
struct SpaceSite {
    std::uint64_t id = 0; // unique inside a system
    SiteKind kind = SiteKind::Star;
    Vector2 position{};
    std::string label;
    bool mapped_on_entry = false;  // star, major celestial; no need to rescan
    bool directional_visible = false;
    int probe_difficulty = 0; // 1..100; 0 means not probe-scannable
};
struct GateLink { std::uint32_t a=0,b=0; bool operational=true; };
struct RouteResult { std::vector<std::uint32_t> systems; std::uint64_t cost=0; };
class GalaxyMap {
public:
    void add_system(SystemDescriptor s);
    void link(std::uint32_t a,std::uint32_t b,bool operational=true);
    void set_link_operational(std::uint32_t a,std::uint32_t b,bool operational);
    void add_site(std::uint32_t system,SpaceSite site);
    const SystemDescriptor& system(std::uint32_t id) const;
    const std::vector<SpaceSite>& sites(std::uint32_t system) const;
    std::vector<SpaceSite> visible_on_entry(std::uint32_t system) const;
    RouteResult route(std::uint32_t from,std::uint32_t to,RoutePolicy policy) const;
private:
    std::map<std::uint32_t,SystemDescriptor> systems_;
    std::map<std::uint32_t,std::vector<SpaceSite>> sites_;
    std::map<std::pair<std::uint32_t,std::uint32_t>, bool> links_;
};
struct ScanObservation {
    PlayerId player=0;
    std::uint32_t system=0;
    std::uint64_t site=0;
    SiteKind kind=SiteKind::Star;
    Vector2 estimated_position{};
    std::uint64_t observed_tick=0; // simulation time; never treat as live data forever
    int confidence=0; // integer 1..100
    bool published=false;
};
struct DirectionalContact {
    std::uint64_t site=0;
    SiteKind kind=SiteKind::Ship;
    int bearing_deg=0; // estimated relative direction, not exact position
    std::int64_t range_band_mm=0; // rounded to 1000 mm; not a warp coordinate
};
class SurveySensors {
public:
    explicit SurveySensors(const GalaxyMap& map) : map_(map) {}
    // Requires a valid system and explicit ship position/heading, no oracle coordinates.
    std::vector<DirectionalContact> directional(std::uint32_t system,Vector2 ship,
        double heading_deg,double cone_deg,std::int64_t range_mm) const;
    // A probe must be explicitly deployed and later recovered for reuse.
    struct Probe { std::uint64_t id=0; PlayerId owner=0; std::uint32_t system=0; Vector2 position{}; bool deployed=false; };
    void register_probe(std::uint64_t id, PlayerId owner);
    bool deploy(std::uint64_t id,PlayerId owner,std::uint32_t system,Vector2 position);
    bool recover(std::uint64_t id,PlayerId owner,std::uint32_t system);
    std::vector<ScanObservation> probe_scan(PlayerId owner,std::uint32_t system,
        std::uint64_t tick,std::int64_t radius_mm,int sensor_strength) const;
    std::size_t deployed_count(PlayerId owner) const;
private:
    const GalaxyMap& map_;
    std::map<std::uint64_t,Probe> probes_;
};
// Server-side persistence: ONLY trusted sensor output is to be recorded.
// This is a testable research prototype, not a public submission endpoint.
class SurveyArchive {
public:
    explicit SurveyArchive(const std::string& sqlite_path);
    ~SurveyArchive();
    SurveyArchive(const SurveyArchive&)=delete;
    SurveyArchive& operator=(const SurveyArchive&)=delete;
    // Upsert newest observation per observer/site. Updates are independently attributed.
    bool record(const ScanObservation& observation);
    std::vector<ScanObservation> personal(PlayerId player,std::uint32_t system) const;
    // Append-only changes made by the trusted scanner. Descending order,
    // bounded to at most 100 records; private to the requesting owner.
    std::vector<ScanObservation> history(PlayerId player,std::uint32_t system,
        std::size_t limit=100) const;
    // Aggregate public data; never leak private observations.
    struct PublicRecord { std::uint32_t system=0;std::uint64_t site=0; SiteKind kind=SiteKind::Star;
        int independent_surveyors=0;int confidence=0;std::uint64_t latest_tick=0;
        Vector2 estimated_position{}; };
    std::vector<PublicRecord> published(std::uint32_t system) const;
private:
    ::sqlite3* db_=nullptr;
};
} // namespace galaxy
