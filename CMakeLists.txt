cmake_minimum_required(VERSION 3.18)
project(SingleGalaxyKernel VERSION 0.3.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer verified upstream amalgamation for fully offline, self-contained builds.
# Without it, keep Linux/system-SDK fallback; no binary is downloaded at build time.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.c" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.h")
    add_library(sqlite_vendored STATIC vendor/sqlite/sqlite3.c)
    target_include_directories(sqlite_vendored PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite")
    target_compile_definitions(sqlite_vendored PRIVATE SQLITE_THREADSAFE=1 SQLITE_DEFAULT_SYNCHRONOUS=3)
    add_library(SQLite::SQLite3 ALIAS sqlite_vendored)
    message(STATUS "Using locally verified bundled SQLite amalgamation")
else()
    find_package(SQLite3 REQUIRED)
    message(STATUS "Using OS SQLite SDK (amalgamation not present)")
endif()
add_library(galaxy_kernel STATIC src/Simulation.cpp src/ColonyResilience.cpp
  src/ColonyRecovery.cpp src/ColonyPolicy.cpp src/PopulationCohorts.cpp
    src/ColonyWaterSupply.cpp src/Persistence.cpp src/EconomyPrototype.cpp src/QuantifiedProduction.cpp src/Inventory.cpp src/InventoryLogistics.cpp src/FactoryLayout.cpp src/BlueprintConstruction.cpp src/HostBlueprintRegistry.cpp src/ClientContract.cpp src/Galaxy.cpp src/Navigation.cpp src/GateTransit.cpp src/Region.cpp
  src/ColdColonyNeeds.cpp src/MacroNeeds.cpp src/ColonySafety.cpp src/ColonySafetyJournal.cpp src/ColonySafetyNotice.cpp src/NpcAppearanceGenome.cpp src/MedicalSocietyBridge.cpp src/DependentIncidentAccounting.cpp src/DependentIncidentJournal.cpp src/DependentGuardianship.cpp src/SplitRescueIntent.cpp src/RegionalCheckpointStore.cpp src/RegionalPopulationAudit.cpp src/RegionalEventScheduler.cpp src/RegionalModeAuthority.cpp src/RegionalFactoryHandoff.cpp src/RegionalProductionNetwork.cpp src/RegionalShipmentReconciliation.cpp src/ShipmentManifest.cpp src/ThirdPartyShipment.cpp src/CanonicalShippingHost.cpp src/ShipmentStatusView.cpp
  src/RegionalHostCommands.cpp src/RegionalInterestManager.cpp src/RegionalCustodyLedger.cpp src/RegionalPersistentWorldBridge.cpp)
target_include_directories(galaxy_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(galaxy_kernel PUBLIC SQLite::SQLite3)
# Compatibility target only; all regional objects have one game-kernel authority.
add_library(galaxy_regional INTERFACE)
target_link_libraries(galaxy_regional INTERFACE galaxy_kernel SQLite::SQLite3)
if(MSVC)
    target_compile_options(galaxy_kernel PRIVATE /W4 /permissive-)
else()
    target_compile_options(galaxy_kernel PRIVATE -Wall -Wextra -Wpedantic)
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04a_tests tests/COLONY_AUTO04A_SafetyTests.cpp)
    target_link_libraries(galaxy_colony_auto04a_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04a_safety COMMAND galaxy_colony_auto04a_tests)
endif()
if(BUILD_TESTING)
    add_executable(galaxy_tests tests/SimulationTests.cpp)
    target_link_libraries(galaxy_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_kernel_tests COMMAND galaxy_tests)
    add_executable(galaxy_persistence_tests tests/PersistenceTests.cpp)
    target_link_libraries(galaxy_persistence_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_persistence_tests COMMAND galaxy_persistence_tests)
    add_executable(galaxy_client_contract_tests tests/ClientContractTests.cpp)
    target_link_libraries(galaxy_client_contract_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_client_contract_tests COMMAND galaxy_client_contract_tests)
    add_executable(galaxy_mining_lifecycle_tests tests/MiningLifecycleTests.cpp)
    target_link_libraries(galaxy_mining_lifecycle_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_mining_lifecycle_tests COMMAND galaxy_mining_lifecycle_tests)
endif()
add_executable(galaxy_persistence_demo src/persistence_demo.cpp)
target_link_libraries(galaxy_persistence_demo PRIVATE galaxy_kernel)
option(BUILD_BENCHMARKS "Build preliminary headless tick microbenchmark" ON)
if(BUILD_BENCHMARKS)
    add_executable(galaxy_benchmark src/benchmark.cpp)
    target_link_libraries(galaxy_benchmark PRIVATE galaxy_kernel)
endif()

add_executable(galaxy_recover_copy src/recover_copy.cpp)
target_link_libraries(galaxy_recover_copy PRIVATE galaxy_kernel)
add_executable(galaxy_persistence_benchmark src/persistence_benchmark.cpp)
target_link_libraries(galaxy_persistence_benchmark PRIVATE galaxy_kernel)

add_executable(galaxy_client_stream_demo src/client_stream_demo.cpp)
target_link_libraries(galaxy_client_stream_demo PRIVATE galaxy_kernel)
add_executable(galaxy_client_wire_producer src/client_wire_producer.cpp)
target_link_libraries(galaxy_client_wire_producer PRIVATE galaxy_kernel)
add_executable(galaxy_client_wire_consumer src/client_wire_consumer.cpp)
target_link_libraries(galaxy_client_wire_consumer PRIVATE galaxy_kernel)

# B4 visual replay recorder uses the actual committed B3 simulation, not sample fixtures.
add_executable(galaxy_visual_scenario_recorder src/visual_scenario_recorder.cpp)
target_link_libraries(galaxy_visual_scenario_recorder PRIVATE galaxy_kernel)

# B4.1 independent, fixed-step real-time host. No network dependency.
add_executable(galaxy_live_scenario_host src/live_scenario_host.cpp)
target_link_libraries(galaxy_live_scenario_host PRIVATE galaxy_kernel)

# B4.3 local command-capable development process: all world writes remain in C++.
add_executable(galaxy_interactive_host src/interactive_host.cpp)
target_link_libraries(galaxy_interactive_host PRIVATE galaxy_kernel)

# B5 game-start station, undock/dock lifecycle and private local hangar acceptance.
if(BUILD_TESTING)
    add_executable(galaxy_station_lifecycle_tests tests/StationLifecycleTests.cpp)
    target_link_libraries(galaxy_station_lifecycle_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_station_lifecycle_tests COMMAND galaxy_station_lifecycle_tests)
endif()

# B5.2 end-to-end live C++ host + local Python relay: optional if Python is installed.
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME galaxy_b52_station_live_e2e
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B52InteractiveEndToEndTests.py
          --executable $<TARGET_FILE:galaxy_interactive_host>
          --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
    endif()
endif()

# B5.3 station-to-station real cargo movement, local markets, credits, recovery.
if(BUILD_TESTING)
    add_executable(galaxy_logistics_tests tests/LogisticsTests.cpp)
    target_link_libraries(galaxy_logistics_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_b53_logistics_tests COMMAND galaxy_logistics_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(NAME galaxy_b53_live_logistics_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B53InteractiveEndToEndTests.py
        --executable $<TARGET_FILE:galaxy_interactive_host>
        --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B5.4/B6.0 connected geography, route policy, directional/probe scan and archive.
if(BUILD_TESTING)
    add_executable(galaxy_geography_scanning_tests tests/GeographyScanningTests.cpp)
    target_link_libraries(galaxy_geography_scanning_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_b54_b60_geography_scanning_tests COMMAND galaxy_geography_scanning_tests)
endif()
add_executable(galaxy_survey_demo src/survey_demo.cpp)
target_link_libraries(galaxy_survey_demo PRIVATE galaxy_kernel)
if(BUILD_TESTING)
    add_executable(galaxy_navigation_tests tests/NavigationTests.cpp)
    target_link_libraries(galaxy_navigation_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_b54_navigation_tests COMMAND galaxy_navigation_tests)
endif()

# B6.1 live scanner and survey archive relay, beyond standalone B6.0 geology tests.
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(NAME galaxy_b61_live_scanning_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B61LiveScanningTests.py
        --executable $<TARGET_FILE:galaxy_interactive_host>
        --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B6.2a headless gate transition state machine. NOT connected to live world yet.
if(BUILD_TESTING)
    add_executable(galaxy_gate_transit_tests tests/GateTransitTests.cpp)
    target_link_libraries(galaxy_gate_transit_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_b62a_gate_transit_tests COMMAND galaxy_gate_transit_tests)
endif()

# B6.2b authoritative multi-system ship ownership and persistent jump integration.
if(BUILD_TESTING)
    add_executable(galaxy_b62b_durable_jumps_tests tests/MultiSystemTransitTests.cpp)
    target_link_libraries(galaxy_b62b_durable_jumps_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_b62b_durable_jumps_tests COMMAND galaxy_b62b_durable_jumps_tests)
endif()
add_executable(galaxy_multisystem_demo src/multisystem_demo.cpp)
target_link_libraries(galaxy_multisystem_demo PRIVATE galaxy_kernel)

# B6.2c per-owner authoritative projections with full snapshot at system changes.
if(BUILD_TESTING)
    add_executable(galaxy_b62c_projection_tests tests/B62cProjectionTests.cpp)
    target_link_libraries(galaxy_b62c_projection_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_b62c_projection_tests COMMAND galaxy_b62c_projection_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(NAME galaxy_b62c_live_multisystem_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B62cLiveJumpTests.py
        --executable $<TARGET_FILE:galaxy_interactive_host>
        --root ${CMAKE_CURRENT_SOURCE_DIR})
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(NAME galaxy_b62c_relay_gate_controls
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B62cRelayTests.py
        --executable $<TARGET_FILE:galaxy_interactive_host>
        --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B6.3 live route advisories, append-only survey revisions and re-entry history.
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b63_live_routes_history_e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B63RouteSurveyEndToEndTests.py
    --executable $<TARGET_FILE:galaxy_interactive_host>)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b63_relay_route_controls
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B63RelayRouteTests.py
    --executable $<TARGET_FILE:galaxy_interactive_host>
    --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B6.4 playable original seven-system region, multiple physical gates per system.
if(BUILD_TESTING)
  add_executable(galaxy_b64_region_tests tests/B64RegionTests.cpp)
  target_link_libraries(galaxy_b64_region_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b64_region_tests COMMAND galaxy_b64_region_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b64_live_multihop_e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B64LiveMultihopTests.py
    --executable $<TARGET_FILE:galaxy_interactive_host>)
endif()

# B6.5 authoritative, durable test-only gate disruptions and routing recovery.
if(BUILD_TESTING)
  add_executable(galaxy_b65_gate_disruption_tests tests/B65GateDisruptionTests.cpp)
  target_link_libraries(galaxy_b65_gate_disruption_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b65_gate_disruption_tests COMMAND galaxy_b65_gate_disruption_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
 add_test(NAME galaxy_b65_live_gate_qa_e2e
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B65LiveGateQATests.py
  --executable $<TARGET_FILE:galaxy_interactive_host>)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
 add_test(NAME galaxy_b65_relay_qa_controls
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B65RelayQATests.py
  --executable $<TARGET_FILE:galaxy_interactive_host>
  --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# QA input automation is a *test build* feature, compiled out for production
# builds with -DBUILD_TESTING=OFF (or -DSG_QA_AUTOMATION=OFF). Runtime additionally
# requires the explicit local relay --qa-test switch.
option(SG_QA_AUTOMATION "Compile local QA input automation (test builds only)" ${BUILD_TESTING})
if(SG_QA_AUTOMATION)
  target_compile_definitions(galaxy_interactive_host PRIVATE SG_QA_AUTOMATION=1)
endif()

# B6.6 deterministic gate operations; serialized with world snapshots and journaled
# via the same authoritative, transactional host write path.
if(BUILD_TESTING)
  add_executable(galaxy_b66_gate_operations_tests tests/B66GateOperationsTests.cpp)
  target_link_libraries(galaxy_b66_gate_operations_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b66_gate_operations_tests COMMAND galaxy_b66_gate_operations_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b66_live_gate_telemetry
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B66LiveGateTelemetryTests.py
    --executable $<TARGET_FILE:galaxy_interactive_host>)
endif()

# B7.0a first true colony start: no prebuilt stations, markets or stargates.
if(BUILD_TESTING)
  add_executable(galaxy_b70_colony_core_tests tests/B70ColonyCoreLoopTests.cpp)
  target_link_libraries(galaxy_b70_colony_core_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70_colony_core_tests COMMAND galaxy_b70_colony_core_tests)
endif()
add_executable(galaxy_colony_cli src/colony_cli.cpp)
target_link_libraries(galaxy_colony_cli PRIVATE galaxy_kernel)
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70_colony_cli_e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70ColonyCliE2E.py
    --executable $<TARGET_FILE:galaxy_colony_cli>)
endif()

# B7.0b one-ship, no-infrastructure authoritative graphical host.
add_executable(galaxy_colony_live_host src/colony_live_host.cpp)
target_link_libraries(galaxy_colony_live_host PRIVATE galaxy_kernel)

if(BUILD_TESTING)
  add_executable(galaxy_b70b_geology_tests tests/B70bGeologyTests.cpp)
  target_link_libraries(galaxy_b70b_geology_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70b_geology_tests COMMAND galaxy_b70b_geology_tests)
endif()

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70b_live_colony_e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70bLiveColonyTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0c phase-specific geological cargo, refining and usable structural goods.
if(BUILD_TESTING)
  add_executable(galaxy_b70c_mineral_industry_tests tests/B70cMineralIndustryTests.cpp)
  target_link_libraries(galaxy_b70c_mineral_industry_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70c_mineral_industry_tests COMMAND galaxy_b70c_mineral_industry_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70c_live_mineral_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70cLiveRelayTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0d finite initial provisions, hydroponics and audited life-support survival.
if(BUILD_TESTING)
  add_executable(galaxy_b70d_life_support_tests tests/B70dLifeSupportTests.cpp)
  target_link_libraries(galaxy_b70d_life_support_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70d_life_support_tests COMMAND galaxy_b70d_life_support_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70d_live_agriculture_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70dLiveAgricultureTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0e physically located farm, finite NPC staffing, automatic food production,
# 3-cycle production assessment, and SQLite recovery (legacy scenarios unchanged).
if(BUILD_TESTING)
  add_executable(galaxy_b70e_colony_farms_tests tests/B70eColonyFarmsTests.cpp)
  target_link_libraries(galaxy_b70e_colony_farms_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70e_colony_farms_tests COMMAND galaxy_b70e_colony_farms_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70e_live_farm_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70eLiveFarmTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0f player-built habitation with finite locally provisioned residents,
# three-cycle comfort assessment, transactional ledger and copy recovery.
if(BUILD_TESTING)
  add_executable(galaxy_b70f_habitation_tests tests/B70fHabitationTests.cpp)
  target_link_libraries(galaxy_b70f_habitation_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70f_habitation_tests COMMAND galaxy_b70f_habitation_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70f_live_habitat_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70fLiveHabitatTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0g: power service, failure, maintenance, journal durability.
if(BUILD_TESTING)
  add_executable(galaxy_b70g_utilities_tests tests/B70gUtilitiesTests.cpp)
  target_link_libraries(galaxy_b70g_utilities_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70g_utilities_tests COMMAND galaxy_b70g_utilities_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70g_live_utility_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70gLiveUtilityTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0h delegated maintenance: housed technicians, finite on-site spares and
# deterministic local NPC repairs; no automatic colony expansion.
if(BUILD_TESTING)
  add_executable(galaxy_b70h_delegated_maintenance_tests tests/B70hDelegatedMaintenanceTests.cpp)
  target_link_libraries(galaxy_b70h_delegated_maintenance_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70h_delegated_maintenance_tests COMMAND galaxy_b70h_delegated_maintenance_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70h_live_maintenance_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70hLiveMaintenanceTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0i derived resilience diagnostics: no changes to save format or authority.
if(BUILD_TESTING)
  add_executable(galaxy_b70i_resilience_tests tests/B70iResilienceTests.cpp)
  target_link_libraries(galaxy_b70i_resilience_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70i_resilience_tests COMMAND galaxy_b70i_resilience_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70i_live_resilience_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70iLiveResilienceTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0j physical community services, occupancy, staff and receipts.
if(BUILD_TESTING)
  add_executable(galaxy_b70j_community_tests tests/B70jCommunityTests.cpp)
  target_link_libraries(galaxy_b70j_community_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70j_community_tests COMMAND galaxy_b70j_community_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70j_live_community_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70jLiveCommunityTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0k bounded first-aid clinic, physical inputs, case recovery and persistence.
if(BUILD_TESTING)
  add_executable(galaxy_b70k_clinic_tests tests/B70kClinicTests.cpp)
  target_link_libraries(galaxy_b70k_clinic_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70k_clinic_tests COMMAND galaxy_b70k_clinic_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70k_live_clinic_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70kLiveClinicTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_b70k_reclaimer_tests tests/B70kReclaimerTests.cpp)
  target_link_libraries(galaxy_b70k_reclaimer_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70k_reclaimer_tests COMMAND galaxy_b70k_reclaimer_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70k_live_reclaimer_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70kLiveReclaimerTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0l — mined water cargo, physically delivered local waterworks, persistence.
if(BUILD_TESTING)
  add_executable(galaxy_b70l_waterworks_tests tests/B70lWaterworksTests.cpp)
  target_link_libraries(galaxy_b70l_waterworks_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70l_waterworks_tests COMMAND galaxy_b70l_waterworks_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70l_live_waterworks_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70lLiveWaterworksTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0m: player-declared policies, finite staffed oversight, live service evidence.
if(BUILD_TESTING)
  add_executable(galaxy_b70m_policy_tests tests/B70mPolicyTests.cpp)
  target_link_libraries(galaxy_b70m_policy_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70m_policy_tests COMMAND galaxy_b70m_policy_tests)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_b70m_live_policy_relay
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/B70mLivePolicyTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py)
endif()

# B7.0n: persistent individual crew identities, bounded real-work XP, one-to-one
# mentorship, and cheap stratified population cohorts for remote-scale studies.
if(BUILD_TESTING)
  add_executable(galaxy_b70n_workforce_tests tests/B70nWorkforceTests.cpp)
  target_link_libraries(galaxy_b70n_workforce_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_b70n_workforce_tests COMMAND galaxy_b70n_workforce_tests)
endif()

# GRSM-01: one canonical historical fact per executed command, committed
# atomically with physical state and verified on restart/recovery.
if(BUILD_TESTING)
  add_executable(galaxy_grsm01_history_tests tests/GRSM01HistoryTests.cpp)
  target_link_libraries(galaxy_grsm01_history_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm01_history_tests COMMAND galaxy_grsm01_history_tests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_grsm01b_physical_tests tests/GRSM01bPhysicalTests.cpp)
  target_link_libraries(galaxy_grsm01b_physical_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm01b_physical_tests COMMAND galaxy_grsm01b_physical_tests)
endif()

# GRSM-02: locally observed incidents and provenance-linked contradictory NPC
# stories, kept separate from immutable physical truth.
if(BUILD_TESTING)
  add_executable(galaxy_grsm02_knowledge_tests tests/GRSM02KnowledgeTests.cpp)
  target_link_libraries(galaxy_grsm02_knowledge_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm02_knowledge_tests COMMAND galaxy_grsm02_knowledge_tests)
endif()

# GRSM-03: overlapping original-workforce groups, proximity-attested
# member-to-member relays, persistent divergent interpretations.
if(BUILD_TESTING)
  add_executable(galaxy_grsm03_social_tests tests/GRSM03SocialTests.cpp)
  target_link_libraries(galaxy_grsm03_social_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm03_social_tests COMMAND galaxy_grsm03_social_tests)

  add_executable(galaxy_grsm03b_trust_tests tests/GRSM03bTrustTests.cpp)
  target_link_libraries(galaxy_grsm03b_trust_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm03b_trust_tests COMMAND galaxy_grsm03b_trust_tests)
  add_executable(galaxy_grsm03c_membership_tests tests/GRSM03cMembershipTests.cpp)
  target_link_libraries(galaxy_grsm03c_membership_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm03c_membership_tests COMMAND galaxy_grsm03c_membership_tests)
  add_executable(galaxy_grsm03c3_relationship_tests tests/GRSM03c3RelationshipTests.cpp)
  target_link_libraries(galaxy_grsm03c3_relationship_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm03c3_relationship_tests COMMAND galaxy_grsm03c3_relationship_tests)
  add_executable(galaxy_grsm03d_group_relations_tests tests/GRSM03dGroupRelationsTests.cpp)
  target_link_libraries(galaxy_grsm03d_group_relations_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm03d_group_relations_tests COMMAND galaxy_grsm03d_group_relations_tests)
  add_executable(galaxy_grsm04a_physical_appraisal_tests tests/GRSM04aPhysicalAppraisalTests.cpp)
  target_link_libraries(galaxy_grsm04a_physical_appraisal_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm04a_physical_appraisal_tests COMMAND galaxy_grsm04a_physical_appraisal_tests)
  add_executable(galaxy_grsm03c2_encounter_tests tests/GRSM03c2EncounterTests.cpp)
  target_link_libraries(galaxy_grsm03c2_encounter_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_grsm03c2_encounter_tests COMMAND galaxy_grsm03c2_encounter_tests)
endif()

# GRSM-04b.A: backed NPC roles (victim / firsthand witness), no phantom actor.
if(BUILD_TESTING)
  add_executable(galaxy_grsm04b_responsibility_tests tests/GRSM04bResponsibilityTests.cpp)
  target_link_libraries(galaxy_grsm04b_responsibility_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_grsm04b_responsibility_tests COMMAND galaxy_grsm04b_responsibility_tests)
endif()

# GRSM-04b.B1: explicit named NPC work and committed physical-effect proof.
if(BUILD_TESTING)
  add_executable(galaxy_grsm04b_named_job_tests tests/GRSM04bNamedJobTests.cpp)
  target_link_libraries(galaxy_grsm04b_named_job_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_grsm04b_named_job_tests COMMAND galaxy_grsm04b_named_job_tests)
endif()

# ECON-02A bounded recipe/worker/work-board domain prototype.
# This is not yet connected to PersistentWorld's inventories or action journal.
if(BUILD_TESTING)
  add_executable(galaxy_econ02a_workshop_tests tests/ECON02AWorkshopTests.cpp)
  target_link_libraries(galaxy_econ02a_workshop_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ02a_workshop_tests COMMAND galaxy_econ02a_workshop_tests)
endif()

# GRSM-04b.B2a: named ship-fabrication assignment and committed effect receipt.
if(BUILD_TESTING)
  add_executable(galaxy_grsm04b_fabrication_tests tests/GRSM04bFabricationTests.cpp)
  target_link_libraries(galaxy_grsm04b_fabrication_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_grsm04b_fabrication_tests COMMAND galaxy_grsm04b_fabrication_tests)
endif()

# ECON-02B0: prototype deterministic off-screen manufacturing kernel.
# No connection to the persistent world/material ledger yet.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b0_quantified_tests tests/ECON02B0QuantifiedTests.cpp)
  target_link_libraries(galaxy_econ02b0_quantified_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ02b0_quantified_tests COMMAND galaxy_econ02b0_quantified_tests)
endif()

# ECON-02B1a: a real player-built outpost's Tier 1 machine runs the
# quantified factory kernel against physically delivered, durable stock.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b1a_outpost_tests tests/ECON02B1aOutpostWorkshopTests.cpp)
  target_link_libraries(galaxy_econ02b1a_outpost_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b1a_outpost_tests COMMAND galaxy_econ02b1a_outpost_tests)
endif()

# ECON-02B1b: independent outpost machine, bounded interstage stock, and backpressure.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b1b_connected_tests tests/ECON02B1bConnectedLineTests.cpp)
  target_link_libraries(galaxy_econ02b1b_connected_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b1b_connected_tests COMMAND galaxy_econ02b1b_connected_tests)
endif()

# ECON-02B1c: authoritative cubic-metre cargo volume and exact factory item stacks.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b1c_cargo_tests tests/ECON02B1cCargoVolumeTests.cpp)
  target_link_libraries(galaxy_econ02b1c_cargo_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b1c_cargo_tests COMMAND galaxy_econ02b1c_cargo_tests)
endif()

# ECON-02B2a: catalogue-backed m3 stacks used by real ship/workshop transfers.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2_inventory_tests tests/ECON02B2InventoryTests.cpp)
  target_link_libraries(galaxy_econ02b2_inventory_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2_inventory_tests COMMAND galaxy_econ02b2_inventory_tests)
endif()

# ECON-02B2b: physically constructed mixed-volume outpost storage.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2b_storage_tests tests/ECON02B2bStorageTests.cpp)
  target_link_libraries(galaxy_econ02b2b_storage_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2b_storage_tests COMMAND galaxy_econ02b2b_storage_tests)
endif()

# ECON-02B2c1: multi-container directed, finite-rate material logistics domain.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2c1_logistics_tests tests/ECON02B2c1LogisticsTests.cpp)
  target_link_libraries(galaxy_econ02b2c1_logistics_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ02b2c1_logistics_tests COMMAND galaxy_econ02b2c1_logistics_tests)
endif()

# ECON-02B2c2: extra real storage bins at a constructed site with SQLite receipts.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2c2_persistent_nodes_tests tests/ECON02B2c2PersistentNodesTests.cpp)
  target_link_libraries(galaxy_econ02b2c2_persistent_nodes_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2c2_persistent_nodes_tests COMMAND galaxy_econ02b2c2_persistent_nodes_tests)
endif()

# ECON-02B2c3: module-backed automatic site transport and journaled effects.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2c3_auto_links_tests tests/ECON02B2c3AutomaticLinksTests.cpp)
  target_link_libraries(galaxy_econ02b2c3_auto_links_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2c3_auto_links_tests COMMAND galaxy_econ02b2c3_auto_links_tests)
endif()

# ECON-02B2d1: player/NPC-authored spatial equipment/conveyor blueprints.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d1_factory_layout_tests tests/ECON02B2d1FactoryLayoutTests.cpp)
  target_link_libraries(galaxy_econ02b2d1_factory_layout_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ02b2d1_factory_layout_tests COMMAND galaxy_econ02b2d1_factory_layout_tests)
endif()

# ECON-02B2d2: real module-backed workshop input/output conveyor connections.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d2_workshop_links_tests tests/ECON02B2d2WorkshopLinksTests.cpp)
  target_link_libraries(galaxy_econ02b2d2_workshop_links_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2d2_workshop_links_tests COMMAND galaxy_econ02b2d2_workshop_links_tests)
endif()

# ECON-02B2d3a: material-backed equipment placement in the persistent outpost.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d3a_installed_placement_tests tests/ECON02B2d3aInstalledPlacementTests.cpp)
  target_link_libraries(galaxy_econ02b2d3a_installed_placement_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2d3a_installed_placement_tests COMMAND galaxy_econ02b2d3a_installed_placement_tests)
endif()

# ECON-02B2d3b: spatial, module-backed directed conveyor cells and durable installation.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d3b_spatial_conveyor_tests tests/ECON02B2d3bSpatialConveyorTests.cpp)
  target_link_libraries(galaxy_econ02b2d3b_spatial_conveyor_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2d3b_spatial_conveyor_tests COMMAND galaxy_econ02b2d3b_spatial_conveyor_tests)
endif()

# ECON-02B2d4a: canonical blueprints compile to real, journaled build commands.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d4a_blueprint_build_tests tests/ECON02B2d4aBlueprintConstructionTests.cpp)
  target_link_libraries(galaxy_econ02b2d4a_blueprint_build_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  add_test(NAME galaxy_econ02b2d4a_blueprint_build_tests COMMAND galaxy_econ02b2d4a_blueprint_build_tests)
endif()

# ECON-02B2d4b: browser ghost factory editor exports the exact native format.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d4b_editor_contract_tests tests/ECON02B2d4bEditorContractTests.cpp)
  target_link_libraries(galaxy_econ02b2d4b_editor_contract_tests PRIVATE galaxy_kernel)
  target_compile_definitions(galaxy_econ02b2d4b_editor_contract_tests PRIVATE
    SG_EDITOR_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ECON02B2d4b_editor_blueprint.sglayout")
  add_test(NAME galaxy_econ02b2d4b_editor_contract_tests COMMAND galaxy_econ02b2d4b_editor_contract_tests)
endif()

# ECON-02B2d4c1: host-owned, durable factory plan registration + cancellation.
if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d4c1_host_plan_tests tests/ECON02B2d4c1HostPlanTests.cpp)
  target_link_libraries(galaxy_econ02b2d4c1_host_plan_tests PRIVATE galaxy_kernel SQLite::SQLite3)
  target_compile_definitions(galaxy_econ02b2d4c1_host_plan_tests PRIVATE
    SG_EDITOR_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ECON02B2d4b_editor_blueprint.sglayout")
  add_test(NAME galaxy_econ02b2d4c1_host_plan_tests COMMAND galaxy_econ02b2d4c1_host_plan_tests)
endif()

# ECON-02B2d4c2: true-world native bridge for same-origin localhost UI.
add_executable(galaxy_factory_host_bridge src/factory_host_bridge.cpp)
target_link_libraries(galaxy_factory_host_bridge PRIVATE galaxy_kernel)

if(BUILD_TESTING)
  add_executable(galaxy_econ02b2d4c2_world_fixture tests/ECON02B2d4c2WorldFixture.cpp)
  target_link_libraries(galaxy_econ02b2d4c2_world_fixture PRIVATE galaxy_kernel)
endif()
find_program(SG_NODE_BINARY node)
if(BUILD_TESTING AND SG_NODE_BINARY)
  add_test(NAME galaxy_econ02b2d4c2_live_editor_bridge
    COMMAND ${SG_NODE_BINARY} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ECON02B2d4c2LiveEditorTests.cjs
    --server ${CMAKE_CURRENT_SOURCE_DIR}/scripts/factory_live_server.js
    --bridge $<TARGET_FILE:galaxy_factory_host_bridge>
    --fixture $<TARGET_FILE:galaxy_econ02b2d4c2_world_fixture>
    --layout ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ECON02B2d4b_editor_blueprint.sglayout)
  set_tests_properties(galaxy_econ02b2d4c2_live_editor_bridge PROPERTIES TIMEOUT 25)
endif()

# v59 integration tests share authoritative galaxy_kernel and never link a second PersistentWorld.
if(BUILD_TESTING)
  add_executable(v59_atomic_migration tests/V59_atomic_migration.cpp)
  target_link_libraries(v59_atomic_migration PRIVATE galaxy_kernel)
  add_test(NAME v59_atomic_migration COMMAND v59_atomic_migration)
  add_executable(v59_atomic_split_rescue tests/V59_atomic_split_rescue.cpp)
  target_link_libraries(v59_atomic_split_rescue PRIVATE galaxy_kernel)
  add_test(NAME v59_atomic_split_rescue COMMAND v59_atomic_split_rescue)
  add_executable(v59_authorization tests/V59_authorization.cpp)
  target_link_libraries(v59_authorization PRIVATE galaxy_kernel)
  add_test(NAME v59_authorization COMMAND v59_authorization)
  add_executable(v59_capacity tests/V59_capacity.cpp)
  target_link_libraries(v59_capacity PRIVATE galaxy_kernel)
  add_test(NAME v59_capacity COMMAND v59_capacity)
  add_executable(v59_constructed_world_inventory tests/V59_constructed_world_inventory.cpp)
  target_link_libraries(v59_constructed_world_inventory PRIVATE galaxy_kernel)
  add_test(NAME v59_constructed_world_inventory COMMAND v59_constructed_world_inventory)
  add_executable(v59_crash_custody tests/V59_crash_custody.cpp)
  target_link_libraries(v59_crash_custody PRIVATE galaxy_kernel)
  add_test(NAME v59_crash_custody COMMAND v59_crash_custody)
  add_executable(v59_crew_split_rescue tests/V59_crew_split_rescue.cpp)
  target_link_libraries(v59_crew_split_rescue PRIVATE galaxy_kernel)
  add_test(NAME v59_crew_split_rescue COMMAND v59_crew_split_rescue)
  add_executable(v59_cross_system tests/V59_cross_system.cpp)
  target_link_libraries(v59_cross_system PRIVATE galaxy_kernel)
  add_test(NAME v59_cross_system COMMAND v59_cross_system)
  add_executable(v59_dependent_custody tests/V59_dependent_custody.cpp)
  target_link_libraries(v59_dependent_custody PRIVATE galaxy_kernel)
  add_test(NAME v59_dependent_custody COMMAND v59_dependent_custody)
  add_executable(v59_dependent_incident_accounting tests/V59_dependent_incident_accounting.cpp)
  target_link_libraries(v59_dependent_incident_accounting PRIVATE galaxy_kernel)
  add_test(NAME v59_dependent_incident_accounting COMMAND v59_dependent_incident_accounting)
  add_executable(v59_dependent_incident_journal tests/V59_dependent_incident_journal.cpp)
  target_link_libraries(v59_dependent_incident_journal PRIVATE galaxy_kernel)
  add_test(NAME v59_dependent_incident_journal COMMAND v59_dependent_incident_journal)
  add_executable(v59_dependent_rescue tests/V59_dependent_rescue.cpp)
  target_link_libraries(v59_dependent_rescue PRIVATE galaxy_kernel)
  add_test(NAME v59_dependent_rescue COMMAND v59_dependent_rescue)
  add_executable(v59_dependent_rescue_outcome tests/V59_dependent_rescue_outcome.cpp)
  target_link_libraries(v59_dependent_rescue_outcome PRIVATE galaxy_kernel)
  add_test(NAME v59_dependent_rescue_outcome COMMAND v59_dependent_rescue_outcome)
  add_executable(v59_dispatch_diversion_settlement tests/V59_dispatch_diversion_settlement.cpp)
  target_link_libraries(v59_dispatch_diversion_settlement PRIVATE galaxy_kernel)
  add_test(NAME v59_dispatch_diversion_settlement COMMAND v59_dispatch_diversion_settlement)
  add_executable(v59_ferry_loss tests/V59_ferry_loss.cpp)
  target_link_libraries(v59_ferry_loss PRIVATE galaxy_kernel)
  add_test(NAME v59_ferry_loss COMMAND v59_ferry_loss)
  add_executable(v59_ferry_loss_integrity tests/V59_ferry_loss_integrity.cpp)
  target_link_libraries(v59_ferry_loss_integrity PRIVATE galaxy_kernel)
  add_test(NAME v59_ferry_loss_integrity COMMAND v59_ferry_loss_integrity)
  add_executable(v59_genesis_replay tests/V59_genesis_replay.cpp)
  target_link_libraries(v59_genesis_replay PRIVATE galaxy_kernel)
  add_test(NAME v59_genesis_replay COMMAND v59_genesis_replay)
  add_executable(v59_household_migration tests/V59_household_migration.cpp)
  target_link_libraries(v59_household_migration PRIVATE galaxy_kernel)
  add_test(NAME v59_household_migration COMMAND v59_household_migration)
  add_executable(v59_legacy_v34 tests/V59_legacy_v34.cpp)
  target_link_libraries(v59_legacy_v34 PRIVATE galaxy_kernel)
  add_test(NAME v59_legacy_v34 COMMAND v59_legacy_v34)
  add_executable(v59_medical_care tests/V59_medical_care.cpp)
  target_link_libraries(v59_medical_care PRIVATE galaxy_kernel)
  add_test(NAME v59_medical_care COMMAND v59_medical_care)
  add_executable(v59_medical_recovery tests/V59_medical_recovery.cpp)
  target_link_libraries(v59_medical_recovery PRIVATE galaxy_kernel)
  add_test(NAME v59_medical_recovery COMMAND v59_medical_recovery)
  add_executable(v59_migration tests/V59_migration.cpp)
  target_link_libraries(v59_migration PRIVATE galaxy_kernel)
  add_test(NAME v59_migration COMMAND v59_migration)
  add_executable(v59_mixed_ems tests/V59_mixed_ems.cpp)
  target_link_libraries(v59_mixed_ems PRIVATE galaxy_kernel)
  add_test(NAME v59_mixed_ems COMMAND v59_mixed_ems)
  add_executable(v59_mixed_integrity tests/V59_mixed_integrity.cpp)
  target_link_libraries(v59_mixed_integrity PRIVATE galaxy_kernel)
  add_test(NAME v59_mixed_integrity COMMAND v59_mixed_integrity)
  add_executable(v59_multi_destination_host tests/V59_multi_destination_host.cpp)
  target_link_libraries(v59_multi_destination_host PRIVATE galaxy_kernel)
  add_test(NAME v59_multi_destination_host COMMAND v59_multi_destination_host)
  add_executable(v59_multi_family_rescue tests/V59_multi_family_rescue.cpp)
  target_link_libraries(v59_multi_family_rescue PRIVATE galaxy_kernel)
  add_test(NAME v59_multi_family_rescue COMMAND v59_multi_family_rescue)
  add_executable(v59_nonparent_guardian tests/V59_nonparent_guardian.cpp)
  target_link_libraries(v59_nonparent_guardian PRIVATE galaxy_kernel)
  add_test(NAME v59_nonparent_guardian COMMAND v59_nonparent_guardian)
  add_executable(v59_ordering tests/V59_ordering.cpp)
  target_link_libraries(v59_ordering PRIVATE galaxy_kernel)
  add_test(NAME v59_ordering COMMAND v59_ordering)
  add_executable(v59_paired_family_handoff tests/V59_paired_family_handoff.cpp)
  target_link_libraries(v59_paired_family_handoff PRIVATE galaxy_kernel)
  add_test(NAME v59_paired_family_handoff COMMAND v59_paired_family_handoff)
  add_executable(v59_paired_household_settlement tests/V59_paired_household_settlement.cpp)
  target_link_libraries(v59_paired_household_settlement PRIVATE galaxy_kernel)
  add_test(NAME v59_paired_household_settlement COMMAND v59_paired_household_settlement)
  add_executable(v59_physical_handoff tests/V59_physical_handoff.cpp)
  target_link_libraries(v59_physical_handoff PRIVATE galaxy_kernel)
  add_test(NAME v59_physical_handoff COMMAND v59_physical_handoff)
  add_executable(v59_production_network_handoff tests/V59_production_network_handoff.cpp)
  target_link_libraries(v59_production_network_handoff PRIVATE galaxy_kernel)
  add_test(NAME v59_production_network_handoff COMMAND v59_production_network_handoff)
  add_executable(v59_real_industry_handoff tests/V59_real_industry_handoff.cpp)
  target_link_libraries(v59_real_industry_handoff PRIVATE galaxy_kernel)
  add_test(NAME v59_real_industry_handoff COMMAND v59_real_industry_handoff)
  add_executable(v59_region_scoped_handoff tests/V59_region_scoped_handoff.cpp)
  target_link_libraries(v59_region_scoped_handoff PRIVATE galaxy_kernel)
  add_test(NAME v59_region_scoped_handoff COMMAND v59_region_scoped_handoff)
  add_executable(v59_safety tests/V59_safety.cpp)
  target_link_libraries(v59_safety PRIVATE galaxy_kernel)
  add_test(NAME v59_safety COMMAND v59_safety)
  add_executable(v59_shared_regional_population tests/V59_shared_regional_population.cpp)
  target_link_libraries(v59_shared_regional_population PRIVATE galaxy_kernel)
  add_test(NAME v59_shared_regional_population COMMAND v59_shared_regional_population)
  add_executable(v59_society_atomic tests/V59_society_atomic.cpp)
  target_link_libraries(v59_society_atomic PRIVATE galaxy_kernel)
  add_test(NAME v59_society_atomic COMMAND v59_society_atomic)
  add_executable(v59_society_birth tests/V59_society_birth.cpp)
  target_link_libraries(v59_society_birth PRIVATE galaxy_kernel)
  add_test(NAME v59_society_birth COMMAND v59_society_birth)
  add_executable(v59_society_reconciliation tests/V59_society_reconciliation.cpp)
  target_link_libraries(v59_society_reconciliation PRIVATE galaxy_kernel)
  add_test(NAME v59_society_reconciliation COMMAND v59_society_reconciliation)
  add_executable(v59_split_rescue_intent tests/V59_split_rescue_intent.cpp)
  target_link_libraries(v59_split_rescue_intent PRIVATE galaxy_kernel)
  add_test(NAME v59_split_rescue_intent COMMAND v59_split_rescue_intent)
  add_executable(v59_strand_rescue tests/V59_strand_rescue.cpp)
  target_link_libraries(v59_strand_rescue PRIVATE galaxy_kernel)
  add_test(NAME v59_strand_rescue COMMAND v59_strand_rescue)
  add_executable(v59_third_party_custody tests/V59_third_party_custody.cpp)
  target_link_libraries(v59_third_party_custody PRIVATE galaxy_kernel)
  add_test(NAME v59_third_party_custody COMMAND v59_third_party_custody)
  add_executable(v59_world_finished_goods_logistics tests/V59_world_finished_goods_logistics.cpp)
  target_link_libraries(v59_world_finished_goods_logistics PRIVATE galaxy_kernel)
  add_test(NAME v59_world_finished_goods_logistics COMMAND v59_world_finished_goods_logistics)
  add_executable(v59_world_finished_goods_return tests/V59_world_finished_goods_return.cpp)
  target_link_libraries(v59_world_finished_goods_return PRIVATE galaxy_kernel)
  add_test(NAME v59_world_finished_goods_return COMMAND v59_world_finished_goods_return)
endif()
# Frozen from the old main-branch kernel, not manufactured by the new parser.
if(BUILD_TESTING)
  add_executable(galaxy_canonical_legacy_v20_replay tests/CanonicalMergeLegacySnapshotTests.cpp)
  target_link_libraries(galaxy_canonical_legacy_v20_replay PRIVATE galaxy_kernel)
  add_test(NAME galaxy_canonical_legacy_v20_replay
    COMMAND galaxy_canonical_legacy_v20_replay
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/canonical_merge/legacy_v20_genesis.snap)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c2coldcolonytests tests/ECON03c2ColdColonyTests.cpp)
  target_link_libraries(galaxy_econ03c2coldcolonytests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c2coldcolonytests COMMAND galaxy_econ03c2coldcolonytests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c3regionalschedulertests tests/ECON03c3RegionalSchedulerTests.cpp)
  target_link_libraries(galaxy_econ03c3regionalschedulertests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c3regionalschedulertests COMMAND galaxy_econ03c3regionalschedulertests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c4regionaldurabilitytests tests/ECON03c4RegionalDurabilityTests.cpp)
  target_link_libraries(galaxy_econ03c4regionaldurabilitytests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c4regionaldurabilitytests COMMAND galaxy_econ03c4regionaldurabilitytests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c5hostcommandstests tests/ECON03c5HostCommandsTests.cpp)
  target_link_libraries(galaxy_econ03c5hostcommandstests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c5hostcommandstests COMMAND galaxy_econ03c5hostcommandstests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c6interestcustodytests tests/ECON03c6InterestCustodyTests.cpp)
  target_link_libraries(galaxy_econ03c6interestcustodytests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c6interestcustodytests COMMAND galaxy_econ03c6interestcustodytests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c7modeauthoritytests tests/ECON03c7ModeAuthorityTests.cpp)
  target_link_libraries(galaxy_econ03c7modeauthoritytests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c7modeauthoritytests COMMAND galaxy_econ03c7modeauthoritytests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c8realworldbridgetests tests/ECON03c8RealWorldBridgeTests.cpp)
  target_link_libraries(galaxy_econ03c8realworldbridgetests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c8realworldbridgetests COMMAND galaxy_econ03c8realworldbridgetests)
endif()

if(BUILD_TESTING)
  add_executable(galaxy_econ03c8bworldreconciliationtests tests/ECON03c8bWorldReconciliationTests.cpp)
  target_link_libraries(galaxy_econ03c8bworldreconciliationtests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_econ03c8bworldreconciliationtests COMMAND galaxy_econ03c8bworldreconciliationtests)
endif()

# DEMOG-05q6e5b8j: read-only native shipping status pipe behind a credential-
# bound localhost session service. Never another world/cargo writer.
add_executable(galaxy_shipping_status_bridge src/shipping_status_bridge.cpp)
target_link_libraries(galaxy_shipping_status_bridge PRIVATE galaxy_kernel)
if(BUILD_TESTING)
  find_program(SG_SHIPPING_NODE node)
  if(SG_SHIPPING_NODE)
    add_test(NAME galaxy_shipping_transport_identity_tests
      COMMAND ${SG_SHIPPING_NODE} --test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ShippingTransportIdentityTests.js)
    set_tests_properties(galaxy_shipping_transport_identity_tests PROPERTIES TIMEOUT 30)
    add_test(NAME galaxy_shipping_transport_real_world_tests
      COMMAND ${SG_SHIPPING_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ShippingTransportIntegrationTests.js
       --producer $<TARGET_FILE:v59_multi_destination_host>
       --bridge $<TARGET_FILE:galaxy_shipping_status_bridge>)
    set_tests_properties(galaxy_shipping_transport_real_world_tests PROPERTIES TIMEOUT 75)

  endif()
endif()

# DEMOG-05q6e5b8k: main colony GUI + v63 credentials + read-only physical freight
# projection from the SAME SQLite save. Native colony host owns its sole writer
# lock; the shipping bridge may read but cannot act as a second writer.
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_integrated_colony_freight_session_e2e
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/DEMOG05q6e5b8kIntegratedColonySessionTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py
      --bridge $<TARGET_FILE:galaxy_shipping_status_bridge>
      --producer $<TARGET_FILE:v59_multi_destination_host>)
  set_tests_properties(galaxy_integrated_colony_freight_session_e2e PROPERTIES TIMEOUT 90)
endif()

# COLONY-AUTO-04B: safety admission and durable evidence on the canonical writer.
if(BUILD_TESTING)
  add_executable(galaxy_colony_auto04b_host_tests tests/COLONY_AUTO04B_AuthoritativeHostTests.cpp)
  target_link_libraries(galaxy_colony_auto04b_host_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_colony_auto04b_host COMMAND galaxy_colony_auto04b_host_tests)
endif()

if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME galaxy_colony_auto04b_authenticated_relay
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/COLONY_AUTO04B_LiveSafetyTests.py
      --host $<TARGET_FILE:galaxy_colony_live_host>
      --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py
      --bridge $<TARGET_FILE:galaxy_shipping_status_bridge>)
    set_tests_properties(galaxy_colony_auto04b_authenticated_relay PROPERTIES TIMEOUT 35)
  endif()
endif()

# COLONY-AUTO-04D.2: threshold notices share the single durable world writer.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d2_tests tests/COLONY_AUTO04D2_DurableResponseTests.cpp)
    target_link_libraries(galaxy_colony_auto04d2_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d2_durable_response COMMAND galaxy_colony_auto04d2_tests)
endif()

# COLONY-AUTO-04D.3: individual NPC emergency outbox and durable delivery.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d3_tests tests/COLONY_AUTO04D3_NpcNoticeTests.cpp)
    target_link_libraries(galaxy_colony_auto04d3_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d3_npc_notice COMMAND galaxy_colony_auto04d3_tests)
endif()

if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d3_repair_tests tests/COLONY_AUTO04D3_PhysicalRepairTests.cpp)
    target_link_libraries(galaxy_colony_auto04d3_repair_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d3_physical_repair COMMAND galaxy_colony_auto04d3_repair_tests)
endif()
