#pragma once
// B5.4/B6.0: standalone geography/navigation and sensor discovery contracts.
// The authoritative simulation remains Simulation/PersistentWorld; no global
// omniscient object stream is exposed to a player by this API.
#include "galaxy/Simulation.hpp"
#include "galaxy/Astronomy.hpp"
#include <cstdint>
#include <sqlite3.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace galaxy {
enum class Sovereignty { NPC, Player, Unclaimed };
enum class SiteKind { Star, Planet, Moon, Station, Ship, AsteroidDeposit, GasCloud, Anomaly, Wormhole, Structure };
enum class RoutePolicy { Shortest, PreferSecure };
enum class DiscoveryUse { Navigation, SettlementSurvey };
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
    AstronomicalProfile astronomy(std::uint32_t system,std::uint64_t galaxy_seed) const;
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
// PLAY-13B.1: different scan intentions share the trusted sensor implementation.
enum class ScanMode { Environmental, Celestial, Anomaly };
// Coarse signature contact never exposes the authoritative target position or class.
struct SignatureContact {
    std::uint64_t site=0;
    Vector2 coarse_center{};
    std::int64_t uncertainty_radius_mm=0;
};
// Host-issued waypoints for existing owned probes; execution requires native
// per-tick movement and genuine observations, not client-side teleportation.
struct RefinementPlan {
    std::uint64_t site=0;
    std::vector<std::pair<std::uint64_t,Vector2>> waypoints;
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
    std::vector<SignatureContact> coarse_signatures(PlayerId owner,std::uint32_t system,
        Vector2 scanner_position,std::int64_t range_mm,int sensor_strength) const;
    std::optional<RefinementPlan> plan_refinement(PlayerId owner,std::uint32_t system,
        std::uint64_t signature,Vector2 scanner_position,std::int64_t discovery_range_mm,
        std::int64_t scan_radius_mm,int sensor_strength) const;
    // Trusted simulation tick moves real registered probe positions gradually.
    // Returns true only once every planned probe reaches its waypoint.
    bool advance_refinement(PlayerId owner,std::uint32_t system,
        std::uint64_t signature,Vector2 scanner_position,
        std::int64_t discovery_range_mm,std::int64_t scan_radius_mm,
        int sensor_strength,std::int64_t max_step_mm);
    // Filter actual native probe evidence by scan intent; no client-supplied sightings.
    std::vector<ScanObservation> scan_mode(PlayerId owner,std::uint32_t system,
        std::uint64_t tick,std::int64_t radius_mm,int sensor_strength,ScanMode mode) const;
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
    // Reusable, owner-private map knowledge for navigation/survey/construction.
    // Not proof that the object still exists; callers must revalidate world state.
    std::optional<ScanObservation> known_site(PlayerId owner,std::uint32_t system,
        std::uint64_t site) const;
    std::vector<ScanObservation> known_sites(PlayerId owner,std::uint32_t system,
        int minimum_confidence=1) const;
    // Trusted migration/bridge: import authoritative v67 world sightings
    // into the reusable B6 survey archive without exposing another owner's data.
    struct SystemCatalogueRecord {
        std::uint64_t first_tick=0,last_tick=0,generator_version=0;
        StellarArchitecture architecture=StellarArchitecture::Single;
        std::uint8_t star_count=0;
        std::uint32_t orbital_bands=0;
    };
    // Immutable first-survey provenance, owner-private, separate from live physics.
    bool record_system_catalogue(PlayerId owner,std::uint32_t system,
        std::uint64_t observed_tick,const AstronomicalProfile& profile);
    std::optional<SystemCatalogueRecord> known_system_catalogue(
        PlayerId owner,std::uint32_t system) const;
    std::size_t import_native_discoveries(const Simulation& world,
        PlayerId owner,std::uint32_t system);
    // A host-observed report enters a durable outbox. Reports become readable
    // no earlier than 10 seconds of game-time after capture and only in range.
    // Retrying delivery after a crash is safe: observation writes are idempotent.
    bool queue_probe_report(const ScanObservation& observation,
        std::uint64_t captured_at_ms);
    std::size_t deliver_probe_reports(PlayerId owner,std::uint64_t now_ms,
        std::int64_t link_distance_mm);
    // Candidate filtering is a recommendation only; it does not authorize
    // docking, gate transit, settlement or construction in the world.
    std::vector<ScanObservation> actionable_sites(PlayerId owner,std::uint32_t system,
        const GalaxyMap& live_map,DiscoveryUse purpose,int minimum_confidence,
        std::uint64_t current_tick,std::uint64_t max_age_ticks) const;
    static constexpr std::int64_t kDeepProbeLinkMm=74798935350000LL;
    static constexpr std::uint64_t kNominalResponseMs=10000ULL;
private:
    ::sqlite3* db_=nullptr;
};
} // namespace galaxy
