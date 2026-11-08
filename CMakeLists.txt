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
add_library(galaxy_kernel STATIC src/PlanetaryAgriculture.cpp src/Simulation.cpp src/ColonyResilience.cpp
  src/ColonyRecovery.cpp src/ColonyPolicy.cpp src/PopulationCohorts.cpp
    src/ColonyWaterSupply.cpp src/Persistence.cpp src/NativeCellProcessRunner.cpp src/EconomyPrototype.cpp src/QuantifiedProduction.cpp src/Industry01Catalog.generated.cpp src/WO2ProfessionCatalog.generated.cpp src/Inventory.cpp src/InventoryLogistics.cpp src/FactoryLayout.cpp src/BlueprintConstruction.cpp src/HostBlueprintRegistry.cpp src/ClientContract.cpp src/Galaxy.cpp src/Navigation.cpp src/GateTransit.cpp src/Region.cpp
  src/ColdColonyNeeds.cpp src/MacroNeeds.cpp src/ColonySafety.cpp src/ColonySafetyJournal.cpp src/ColonySafetyNotice.cpp src/ColonyDefense.cpp src/ColonyConflict.cpp src/TerritoryJournal.cpp src/CyberIncidentJournal.cpp src/ColonyEvacuation.cpp src/ColonyFamilyDeparture.cpp src/NpcAppearanceGenome.cpp src/MedicalSocietyBridge.cpp src/DependentIncidentAccounting.cpp src/DependentIncidentJournal.cpp src/DependentGuardianship.cpp src/SplitRescueIntent.cpp src/RegionalCheckpointStore.cpp src/RegionalPopulationAudit.cpp src/RegionalEventScheduler.cpp src/RegionalModeAuthority.cpp src/RegionalFactoryHandoff.cpp src/RegionalProductionNetwork.cpp src/RegionalShipmentReconciliation.cpp src/ShipmentManifest.cpp src/ThirdPartyShipment.cpp src/CanonicalShippingHost.cpp src/ShipmentStatusView.cpp
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

# AUTO-04D3E: actual rescue-ferry physical passenger custody, not whole-colony evacuation.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d3_ferry_tests tests/COLONY_AUTO04D3_FerryRescueTests.cpp)
    target_link_libraries(galaxy_colony_auto04d3_ferry_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d3_ferry_rescue COMMAND galaxy_colony_auto04d3_ferry_tests)
endif()

# COLONY-AUTO-04D.4: cargo and named passenger life support custody.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4_custody_tests tests/COLONY_AUTO04D4_ResourceCustodyTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4_custody_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4_resource_custody COMMAND galaxy_colony_auto04d4_custody_tests)
endif()

# AUTO-04D4B: whole-colony roster and physical receiving capacity survey.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4_manifest_tests tests/COLONY_AUTO04D4_DepartureManifestTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4_manifest_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4_departure_manifest COMMAND galaxy_colony_auto04d4_manifest_tests)
endif()

# AUTO-04D.4C: full bounded-adult colony departure, durable v58 replay.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4c_departure_tests tests/COLONY_AUTO04D4C_AtomicDepartureTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4c_departure_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4c_atomic_departure COMMAND galaxy_colony_auto04d4c_departure_tests)
endif()

if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4c_staffed_tests tests/COLONY_AUTO04D4C_StaffedColonyDepartureTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4c_staffed_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4c_staffed_colony COMMAND galaxy_colony_auto04d4c_staffed_tests)
endif()

# 04D.4D1: real ART02 birth/guardian/census closure, no phantom child rescue.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4d_family_tests tests/COLONY_AUTO04D4D_FamilyProofTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4d_family_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4d_family_proof COMMAND galaxy_colony_auto04d4d_family_tests)
endif()

# AUTO-04D.4D2A: ART02 born-child physical occupancy, local needs and replay.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4d2_physical_tests tests/COLONY_AUTO04D4D2_PhysicalDependentTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4d2_physical_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4d2_physical_dependents COMMAND galaxy_colony_auto04d4d2_physical_tests)
endif()

# AUTO-04D.4D2B1: native all-person colony transfer of physical dependents.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4d2b_native_family_tests tests/COLONY_AUTO04D4D2B_NativeFamilyTransferTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4d2b_native_family_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4d2b_native_family COMMAND galaxy_colony_auto04d4d2b_native_family_tests)
endif()

# AUTO-04D.4D2B2: one actual SQLite world + ART02 + ECON03 family departure.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4d2b2_atomic_family_tests tests/COLONY_AUTO04D4D2B2_AtomicFamilyTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4d2b2_atomic_family_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4d2b2_atomic_family COMMAND galaxy_colony_auto04d4d2b2_atomic_family_tests)
endif()

# AUTO-04D.4D2C: multiple physical children and independent guardian custody
# across a real interrupted cold/detailed region handoff.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d4d2c_mixed_guardians_tests
        tests/COLONY_AUTO04D4D2C_MixedGuardiansFaultMatrixTests.cpp)
    target_link_libraries(galaxy_colony_auto04d4d2c_mixed_guardians_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d4d2c_mixed_guardians
        COMMAND galaxy_colony_auto04d4d2c_mixed_guardians_tests)
endif()

# AUTO-04D.5A: observed NPC leader and real multi-recipient autonomous rescue
# admission, with the existing v76 one-transaction family execution.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d5a_autonomous_emergency_tests
        tests/COLONY_AUTO04D5A_AutonomousEmergencyTests.cpp)
    target_link_libraries(galaxy_colony_auto04d5a_autonomous_emergency_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d5a_autonomous_emergency
        COMMAND galaxy_colony_auto04d5a_autonomous_emergency_tests)
endif()

# AUTO-04D.5B: host-loop NPC alert consumption and SQLite-only rescue replay.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d5b_durable_supervisor_tests
        tests/COLONY_AUTO04D5B_DurableSupervisorTests.cpp)
    target_link_libraries(galaxy_colony_auto04d5b_durable_supervisor_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d5b_durable_supervisor
        COMMAND galaxy_colony_auto04d5b_durable_supervisor_tests)
endif()

# AUTO-04D.5C1: physical emergency habitat supply from staffed colony ship.
if(BUILD_TESTING)
    add_executable(galaxy_colony_auto04d5c1_npc_resupply_tests
        tests/COLONY_AUTO04D5C1_NpcResupplyTests.cpp)
    target_link_libraries(galaxy_colony_auto04d5c1_npc_resupply_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_auto04d5c1_npc_resupply
        COMMAND galaxy_colony_auto04d5c1_npc_resupply_tests)
endif()

# DEF-01A: local crew watch evidence; not a weapon or attack simulation.
if(BUILD_TESTING)
    add_executable(galaxy_colony_defense01a_watch_tests
        tests/COLONY_DEFENSE01A_LocalWatchTests.cpp)
    target_link_libraries(galaxy_colony_defense01a_watch_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_defense01a_local_watch
        COMMAND galaxy_colony_defense01a_watch_tests)
endif()

# CONFLICT-01A: emerging aggression from real NPC agendas and witnessed targets.
if(BUILD_TESTING)
    add_executable(galaxy_colony_conflict01a_aggressive_intent_tests
        tests/COLONY_CONFLICT01A_AggressiveIntentTests.cpp)
    target_link_libraries(galaxy_colony_conflict01a_aggressive_intent_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_conflict01a_aggressive_intent
        COMMAND galaxy_colony_conflict01a_aggressive_intent_tests)
endif()

# CONFLICT-01B1: one real event can generate different subjective colony
# relationships; later real incidents revise that score without altering facts.
if(BUILD_TESTING)
    add_executable(galaxy_colony_conflict01b1_grievance_tests
        tests/COLONY_CONFLICT01B1_GrievanceBeliefTests.cpp)
    target_link_libraries(galaxy_colony_conflict01b1_grievance_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_conflict01b1_grievance
        COMMAND galaxy_colony_conflict01b1_grievance_tests)
endif()

# CONFLICT-01B2: epoch-fenced diplomatic strategy never rewrites historical intent.
if(BUILD_TESTING)
    add_executable(galaxy_colony_conflict01b2_diplomacy_epoch_tests
        tests/COLONY_CONFLICT01B2_DiplomacyEpochTests.cpp)
    target_link_libraries(galaxy_colony_conflict01b2_diplomacy_epoch_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_conflict01b2_diplomacy_epoch
        COMMAND galaxy_colony_conflict01b2_diplomacy_epoch_tests)
endif()

# CONFLICT-01B3: physical NPC governance succession and historical intent replay.
if(BUILD_TESTING)
    add_executable(galaxy_colony_conflict01b3_governance_tests
        tests/COLONY_CONFLICT01B3_GovernanceEpochTests.cpp)
    target_link_libraries(galaxy_colony_conflict01b3_governance_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_conflict01b3_governance
        COMMAND galaxy_colony_conflict01b3_governance_tests)
endif()

# EXIT-01A: individual real-NPC critical survival pressure, never an election.
if(BUILD_TESTING)
    add_executable(galaxy_colony_exit01a_survival_tests
        tests/COLONY_EXIT01A_SurvivalPressureTests.cpp)
    target_link_libraries(galaxy_colony_exit01a_survival_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_exit01a_survival
        COMMAND galaxy_colony_exit01a_survival_tests)
endif()

# EXIT-01B: real player-owner-accepted NPC immigration with existing
# HostEmbarkPassengers / HostDisembarkPassengers / HostSettleMigrant and
# atomic native ART02 + ECON03 society and workforce settlement.
if(BUILD_TESTING)
    add_executable(galaxy_colony_exit01b_immigration_tests
        tests/COLONY_EXIT01B_PhysicalVoluntaryImmigrationTests.cpp)
    target_link_libraries(galaxy_colony_exit01b_immigration_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_exit01b_immigration
        COMMAND galaxy_colony_exit01b_immigration_tests)
endif()

# EXIT-01C: individual history, directional social appraisal and recovered
# personal decision; the actual immigrant remains the original ART02 record.
if(BUILD_TESTING)
    add_executable(galaxy_colony_exit01c_personal_tests
        tests/COLONY_EXIT01C_PersonalHistoryTests.cpp)
    target_link_libraries(galaxy_colony_exit01c_personal_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_exit01c_personal
        COMMAND galaxy_colony_exit01c_personal_tests)
endif()

# EXIT-01D: deterministic native-host observer/selector with authenticated
# receiving-owner route; real boarding and ART02/ECON03 immigration follow.
if(BUILD_TESTING)
    add_executable(galaxy_colony_exit01d_scheduler_tests
        tests/COLONY_EXIT01D_HostMigrationSchedulerTests.cpp)
    target_link_libraries(galaxy_colony_exit01d_scheduler_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_exit01d_scheduler
        COMMAND galaxy_colony_exit01d_scheduler_tests)
endif()

# EXIT-01D second regression: fixed 8-tick host cadence, bounded rotating
# roster, duplicate suppression and conservative no-governor/no-offer refusal.
if(BUILD_TESTING)
    add_executable(galaxy_colony_exit01d_bounded_tests
        tests/COLONY_EXIT01D_BoundedSchedulerTests.cpp)
    target_link_libraries(galaxy_colony_exit01d_bounded_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_colony_exit01d_bounded
        COMMAND galaxy_colony_exit01d_bounded_tests)
endif()

# PLAY-01: a single finite-crew opening, restarting between mining, production
# and first outpost, with recovered-copy conservation checks.
if(BUILD_TESTING)
  add_executable(galaxy_play01_opening_tests
    tests/PLAY01_FirstOutpostRegressionTests.cpp)
  target_link_libraries(galaxy_play01_opening_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play01_opening COMMAND galaxy_play01_opening_tests)
endif()

# PLAY-04: physically persistent ship yaw, construction, real outpost bay
# custody, and recovery of ship/bay ore across world restarts.
if(BUILD_TESTING)
  add_executable(galaxy_play04_yaw_cargo_tests
    tests/PLAY04_YawCargoReturnTests.cpp)
  target_link_libraries(galaxy_play04_yaw_cargo_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play04_yaw_cargo COMMAND galaxy_play04_yaw_cargo_tests)
endif()

# PLAY-08 native training combat: authorized firing, range/ammo/cooldown,
# persistent hull damage and physically durable wreck conversion.
if(BUILD_TESTING)
  add_executable(galaxy_play08_training_combat_tests
    tests/PLAY08_TrainingCombatTests.cpp)
  target_link_libraries(galaxy_play08_training_combat_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play08_training_combat COMMAND galaxy_play08_training_combat_tests)
endif()

# PLAY-09 physically conserved training wreck material, live cargo authority
# and restart/recovery-copy validation.
if(BUILD_TESTING)
  add_executable(galaxy_play09_wreck_salvage_tests
    tests/PLAY09_WreckSalvageTests.cpp)
  target_link_libraries(galaxy_play09_wreck_salvage_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play09_wreck_salvage COMMAND galaxy_play09_wreck_salvage_tests)
endif()

# PLAY-10 authoritative durable local surveying and first-discovery records.
if(BUILD_TESTING)
  add_executable(galaxy_play10_exploration_tests
    tests/PLAY10_ExplorationPersistenceTests.cpp)
  target_link_libraries(galaxy_play10_exploration_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play10_exploration COMMAND galaxy_play10_exploration_tests)
endif()

# PLAY-11 hardpoint fitting, installed survey equipment, conservation,
# strict module/rack snapshot recovery, and persistent discovery gating.
if(BUILD_TESTING)
  add_executable(galaxy_play11_fitting_tests
    tests/PLAY11_FittingExplorationTests.cpp)
  target_link_libraries(galaxy_play11_fitting_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play11_fitting COMMAND galaxy_play11_fitting_tests)
endif()

# PLAY-12 owner-authorized physical survey probes, accounting and durable recovery.
if(BUILD_TESTING)
  add_executable(galaxy_play12_probe_tests tests/PLAY12_ProbeLifecycleTests.cpp)
  target_link_libraries(galaxy_play12_probe_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play12_probes COMMAND galaxy_play12_probe_tests)
endif()

# PLAY-13 host-supervised geological drone flight and physical core sampling.
if(BUILD_TESTING)
  add_executable(galaxy_play13_drone_tests tests/PLAY13_GeologicalDroneTests.cpp)
  target_link_libraries(galaxy_play13_drone_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13_drones COMMAND galaxy_play13_drone_tests)
endif()

# PLAY-13C.2: host-custody celestial arrival preflight, built against real kernel.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_celestial_transition_tests
    tests/PLAY13C_CelestialTransitionTests.cpp)
  target_link_libraries(galaxy_play13c_celestial_transition_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_celestial_transition
    COMMAND galaxy_play13c_celestial_transition_tests)
endif()

# PLAY-13C.2B: persisted v71 per-ship frame custody, strict restore and recover-copy.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_durable_frames_tests
    tests/PLAY13C_DurableShipFramesTests.cpp)
  target_link_libraries(galaxy_play13c_durable_frames_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_durable_frames
    COMMAND galaxy_play13c_durable_frames_tests)
endif()

# PLAY-13C.2C: actual authority-scoped ship frame migration with SQLite
# durable pending/completed receipts, cross-frame isolation and restart.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_actual_transit_tests
    tests/PLAY13C_ActualFrameTransitTests.cpp)
  target_link_libraries(galaxy_play13c_actual_transit_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_actual_transit
    COMMAND galaxy_play13c_actual_transit_tests)
endif()

# PLAY-13C.3: frame-local owner projections, client deltas, scanner visibility
# and SQLite restart isolation across physical celestial arrivals.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_frame_visibility_tests
    tests/PLAY13C_FrameVisibilityTests.cpp)
  target_link_libraries(galaxy_play13c_frame_visibility_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_frame_visibility
    COMMAND galaxy_play13c_frame_visibility_tests)
endif()

# PLAY-13C.4: generated deposits are debited from finite existing ore,
# spatially anchored, exactly persisted, mined, and never reseeded.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_local_region_tests
    tests/PLAY13C_LocalRegionGenerationTests.cpp)
  target_link_libraries(galaxy_play13c_local_region_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_local_region
    COMMAND galaxy_play13c_local_region_tests)
endif()

# PLAY-13C.5: independently verified persistent geology, deterministic
# environment identity and mass-conserving mineral separation.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_local_environment_tests
    tests/PLAY13C_LocalEnvironmentTests.cpp)
  target_link_libraries(galaxy_play13c_local_environment_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_local_environment
    COMMAND galaxy_play13c_local_environment_tests)
endif()

# PLAY-13C.6A read-only frontier resource and life-support viability acceptance.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_expedition_assessment_tests
    tests/PLAY13C_ExpeditionAssessmentTests.cpp)
  target_link_libraries(galaxy_play13c_expedition_assessment_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_expedition_assessment
    COMMAND galaxy_play13c_expedition_assessment_tests)
endif()

# PLAY-13C.6B: sparse, physically unbuilt oasis prospects, discovery privacy
# and no free resource/infrastructure injection.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_rare_oasis_tests
    tests/PLAY13C_RareOasisTests.cpp)
  target_link_libraries(galaxy_play13c_rare_oasis_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_rare_oasis
    COMMAND galaxy_play13c_rare_oasis_tests)
endif()

# PLAY-13C.6C durable source-backed water/nutrients, no free oasis matter.
if(BUILD_TESTING)
  add_executable(galaxy_play13c_physical_oasis_tests
    tests/PLAY13C_PhysicalOasisResourcesTests.cpp)
  target_link_libraries(galaxy_play13c_physical_oasis_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13c_physical_oasis
    COMMAND galaxy_play13c_physical_oasis_tests)
endif()

# PLAY-13D.1 physical planetary agriculture custody kernel, first native gate.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_planetary_agriculture_tests
    tests/PLAY13D_PlanetaryAgricultureTests.cpp)
  target_link_libraries(galaxy_play13d_planetary_agriculture_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_planetary_agriculture
    COMMAND galaxy_play13d_planetary_agriculture_tests)
endif()

# PLAY-13D.2A multi-region planetary environment survey acceptance.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_planetary_regions_tests tests/PLAY13D_PlanetaryRegionsTests.cpp)
  target_link_libraries(galaxy_play13d_planetary_regions_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_planetary_regions COMMAND galaxy_play13d_planetary_regions_tests)
endif()

# PLAY-13D.2C dryland landforms, arroyos, rivers, canyons and caverns.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_planetary_landscape_tests tests/PLAY13D_PlanetaryLandscapeTests.cpp)
  target_link_libraries(galaxy_play13d_planetary_landscape_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_planetary_landscape COMMAND galaxy_play13d_planetary_landscape_tests)
endif()

# PLAY-13D PGF-01A: versioned procedural generator adapter contract.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_planetary_framework_tests tests/PLAY13D_PlanetaryFrameworkTests.cpp)
  target_link_libraries(galaxy_play13d_planetary_framework_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_planetary_framework COMMAND galaxy_play13d_planetary_framework_tests)
endif()

# PGF-01B: connected planetary terrain and watershed acceptance.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_planetary_watershed_tests
    tests/PLAY13D_PlanetaryWatershedTests.cpp)
  target_link_libraries(galaxy_play13d_planetary_watershed_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_planetary_watershed
    COMMAND galaxy_play13d_planetary_watershed_tests)
endif()

# PGF-01B.2 single-source planetary geography over authoritative watershed cells.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_planetary_geography_tests
    tests/PLAY13D_PlanetaryGeographyTests.cpp)
  target_link_libraries(galaxy_play13d_planetary_geography_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_planetary_geography
    COMMAND galaxy_play13d_planetary_geography_tests)
endif()

# PGF-01B.2: authenticated real host planetary survey and saved restart.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_live_planet_survey_tests
    tests/PLAY13D_LivePlanetSurveyTests.cpp)
  target_link_libraries(galaxy_play13d_live_planet_survey_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_live_planet_survey
    COMMAND galaxy_play13d_live_planet_survey_tests)
endif()

# PLAY-13D.3 source-backed planetary site construction/farm/extraction persistence.
if(BUILD_TESTING)
  # PLAY-13D: control roles share one physical ship and inventory model.
  add_executable(galaxy_play13d_ship_control_tests
    tests/PLAY13D_ShipControlTests.cpp)
  target_link_libraries(galaxy_play13d_ship_control_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_ship_control
    COMMAND galaxy_play13d_ship_control_tests)
  # One canonical goods manifest for named NPCs, resources and equipment.
  add_executable(galaxy_play13d_universal_transport_tests
    tests/PLAY13D_UniversalTransportTests.cpp)
  target_link_libraries(galaxy_play13d_universal_transport_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_universal_transport
    COMMAND galaxy_play13d_universal_transport_tests)
  # A single transport-mode-independent NPC migration admission policy.
  add_executable(galaxy_play13d_universal_migration_tests
    tests/PLAY13D_UniversalNpcMigrationTests.cpp)
  target_link_libraries(galaxy_play13d_universal_migration_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_universal_migration
    COMMAND galaxy_play13d_universal_migration_tests)
  # Shared identity/arrival evidence for ferry and planetary shuttle adapters.
  add_executable(galaxy_play13d_shared_transport_tests
    tests/PLAY13D_SharedTransportIdentityTests.cpp)
  target_link_libraries(galaxy_play13d_shared_transport_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_shared_transport
    COMMAND galaxy_play13d_shared_transport_tests)
  # PLAY-13D.4C.9B: verify a proposed canonical migration transition.
  add_executable(galaxy_play13d_settlement_transition_tests
    tests/PLAY13D_PlanetarySettlementTransitionTests.cpp)
  target_link_libraries(galaxy_play13d_settlement_transition_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_settlement_transition
    COMMAND galaxy_play13d_settlement_transition_tests)
  # PLAY-13D.4C.9A: pure fail-closed permanent residency admission policy.
  add_executable(galaxy_play13d_settlement_admission_tests
    tests/PLAY13D_PlanetarySettlementAdmissionTests.cpp)
  target_link_libraries(galaxy_play13d_settlement_admission_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_settlement_admission
    COMMAND galaxy_play13d_settlement_admission_tests)
  add_executable(galaxy_play13d_regional_facilities_tests
    tests/PLAY13D_RegionalFacilitiesTests.cpp)
  target_link_libraries(galaxy_play13d_regional_facilities_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_regional_facilities
    COMMAND galaxy_play13d_regional_facilities_tests)
  # SHIP-01C: same full planetary/ship-freight test, except the
  # carrier MUST be built from rock, crewed, fitted and provisioned in-world.
  add_test(NAME galaxy_ship01c_manufactured_freight
    COMMAND galaxy_play13d_regional_facilities_tests --manufactured-only)
  set_tests_properties(galaxy_ship01c_manufactured_freight
    PROPERTIES TIMEOUT 300)
endif()

# PLAY-13D.4C.1: isolated physical planetary shuttle manifest/NPC custody domain.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_shuttle_custody_tests
    tests/PLAY13D_ShuttleCustodyTests.cpp)
  target_link_libraries(galaxy_play13d_shuttle_custody_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_play13d_shuttle_custody
    COMMAND galaxy_play13d_shuttle_custody_tests)
endif()

# PLAY-13D.4C.5: real authoritative SQLite fixture for HTTP live-host E2E.
if(BUILD_TESTING)
  add_executable(galaxy_play13d_shuttle_host_fixture
    tests/PLAY13D_ShuttleHostFixture.cpp)
  target_link_libraries(galaxy_play13d_shuttle_host_fixture PRIVATE galaxy_kernel)
endif()

# SHIP-01A.0: pure read-only preflight against actual physical colony inventory
# and named NPCs. This test does NOT construct a ship or spend resources.
if(BUILD_TESTING)
  add_executable(galaxy_ship01a_preflight_tests
    tests/SHIP01A_PreflightTests.cpp)
  target_link_libraries(galaxy_ship01a_preflight_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship01a_preflight
    COMMAND galaxy_ship01a_preflight_tests)
endif()

# SHIP-01A.1: authoritative eight-tick physical orbital shipyard construction,
# exact modules embodied in facility, full SQLite failpoint / restart test.
if(BUILD_TESTING)
  add_executable(galaxy_ship01a_construction_tests
    tests/SHIP01A_ConstructionTests.cpp)
  target_link_libraries(galaxy_ship01a_construction_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship01a_construction
    COMMAND galaxy_ship01a_construction_tests)
endif()

# SHIP-01B.1 — real six-module, twelve-tick freighter hull construction,
# committed entity creation, same-frame launch and SQLite restart/failpoints.
if(BUILD_TESTING)
  add_executable(galaxy_ship01b_first_freighter_tests
    tests/SHIP01B_FirstFreighterTests.cpp)
  target_link_libraries(galaxy_ship01b_first_freighter_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship01b_first_freighter
    COMMAND galaxy_ship01b_first_freighter_tests)
endif()

# SHIP-01B.2 — real same-berth NPC crew, stocked life support and paid scanner
# with v92 receipt, exact-once SQLite transfer and authorized first flight.
if(BUILD_TESTING)
  add_executable(galaxy_ship01b2_commission_freighter_tests
    tests/SHIP01B2_CommissionFreighterTests.cpp)
  target_link_libraries(galaxy_ship01b2_commission_freighter_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship01b2_commission_freighter
    COMMAND galaxy_ship01b2_commission_freighter_tests)
endif()

# FLIGHT-02: exact same original two-axis Navigation helper and
# native ship thrust/dock/undock controls, shared v96 NPC flight ledger.
if(BUILD_TESTING)
  add_executable(galaxy_flight02_local_docking_tests
    tests/FLIGHT02_LocalDockingTests.cpp)
  target_link_libraries(galaxy_flight02_local_docking_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_flight02_local_docking
    COMMAND galaxy_flight02_local_docking_tests)
  set_tests_properties(galaxy_flight02_local_docking
    PROPERTIES TIMEOUT 300)
endif()

# FLIGHT-01: one mission-neutral NPC ship navigator using the
# original host celestial-transit physical gate, committed receipts,
# real named pilots and v95 SQLite crash/recovery, not a freight autopilot.
if(BUILD_TESTING)
  add_test(NAME galaxy_flight01_unified_npc_navigation
    COMMAND galaxy_play13d_regional_facilities_tests --npc-autonomous-nav)
  set_tests_properties(galaxy_flight01_unified_npc_navigation
    PROPERTIES TIMEOUT 300)
endif()

# WORK-01C: execute owner-delegated original local shuttle and sealed
# intercolony freight, with the full real pilot, flight, hold, arrival receipt,
# SQLite fault/restart acceptance fixture (no second transport engine).
if(BUILD_TESTING)
  add_test(NAME galaxy_work01c_native_transport_executors
    COMMAND galaxy_play13d_regional_facilities_tests --delegated-work)
  set_tests_properties(galaxy_work01c_native_transport_executors
    PROPERTIES TIMEOUT 300)
endif()

# WORK-01B.3: original colony safety risk and bounded deterministic
# fairness on real customer work, plus live original funding preview.
if(BUILD_TESTING)
  add_executable(galaxy_work01b3_emergency_fairness_tests
    tests/WORK01B3_EmergencyFairnessTests.cpp)
  target_link_libraries(galaxy_work01b3_emergency_fairness_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_work01b3_emergency_fairness
    COMMAND galaxy_work01b3_emergency_fairness_tests)
  set_tests_properties(galaxy_work01b3_emergency_fairness PROPERTIES TIMEOUT 180)
endif()

# WORK-01B.2: canonical worker qualifications and original physical
# one-slip yard capacity, native workforce duty and collision regression.
if(BUILD_TESTING)
  add_executable(galaxy_work01b2_native_capacity_tests
    tests/WORK01B2_CapacityTests.cpp)
  target_link_libraries(galaxy_work01b2_native_capacity_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_work01b2_native_capacity
    COMMAND galaxy_work01b2_native_capacity_tests)
  set_tests_properties(galaxy_work01b2_native_capacity PROPERTIES TIMEOUT 180)
endif()

# WORK-01B: typed real-authority capability registry, deterministic priority
# and same-tick named-worker planning; existing v94 SQLite records unchanged.
if(BUILD_TESTING)
  add_executable(galaxy_work01b_work_planner_tests
    tests/WORK01B_WorkPlannerTests.cpp)
  target_link_libraries(galaxy_work01b_work_planner_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_work01b_work_planner
    COMMAND galaxy_work01b_work_planner_tests)
  set_tests_properties(galaxy_work01b_work_planner PROPERTIES TIMEOUT 180)
endif()

# WORK-01A native v94 universal work ledger + actual NPC fulfillment adapter.
# Uses original galaxy_kernel, existing transport, shipyard, and NPC records.
if(BUILD_TESTING)
  add_executable(galaxy_work01a_universal_fulfillment_tests
    tests/WORK01A_UniversalFulfillmentTests.cpp)
  target_link_libraries(galaxy_work01a_universal_fulfillment_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_work01a_universal_fulfillment
    COMMAND galaxy_work01a_universal_fulfillment_tests)
  set_tests_properties(galaxy_work01a_universal_fulfillment PROPERTIES TIMEOUT 180)
endif()

# SHIP-01F: two separate owners placing, accepting, physically funding
# six real fabricated modules, building and transferring one hull, followed
# by purchaser named-NPC commissioning and SQLite crash recovery.
if(BUILD_TESTING)
  add_executable(galaxy_ship01f_customer_shipyard_tests
    tests/SHIP01F_CustomerShipyardTests.cpp)
  target_link_libraries(galaxy_ship01f_customer_shipyard_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship01f_customer_shipyard
    COMMAND galaxy_ship01f_customer_shipyard_tests)
  set_tests_properties(galaxy_ship01f_customer_shipyard PROPERTIES TIMEOUT 180)
endif()

# SHIP-01D: native fleet list/selection and zero-privilege camera focus tests.
if(BUILD_TESTING)
  add_executable(galaxy_ship01d_fleet_selection_tests
    tests/SHIP01D_FleetSelectionTests.cpp)
  target_link_libraries(galaxy_ship01d_fleet_selection_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship01d_fleet_selection
    COMMAND galaxy_ship01d_fleet_selection_tests)
  set_tests_properties(galaxy_ship01d_fleet_selection PROPERTIES TIMEOUT 180)
endif()

# FLIGHT-03 universal physical-object approach solver, no second movement engine.
if(BUILD_TESTING)
  add_executable(galaxy_flight03_object_approach_tests
    tests/FLIGHT03_ObjectApproachTests.cpp)
  target_link_libraries(galaxy_flight03_object_approach_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_flight03_object_approach
    COMMAND galaxy_flight03_object_approach_tests)
endif()

# FLIGHT-04 read-only physical and receipt preflight: never an interaction grant.
if(BUILD_TESTING)
  add_executable(galaxy_flight04_interaction_preflight_tests
    tests/FLIGHT04_ShipInteractionPreflightTests.cpp)
  target_link_libraries(galaxy_flight04_interaction_preflight_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_flight04_interaction_preflight
    COMMAND galaxy_flight04_interaction_preflight_tests)
endif()

# FLIGHT-04B: adapters call ORIGINAL persistent freight and dependent rescue executors.
if(BUILD_TESTING)
  add_executable(galaxy_flight04b_interaction_executors_tests
    tests/FLIGHT04B_InteractionExecutorsTests.cpp)
  target_link_libraries(galaxy_flight04b_interaction_executors_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_flight04b_interaction_executors
    COMMAND galaxy_flight04b_interaction_executors_tests)
endif()

# FLIGHT-03: original NPC navigator, native ship pursuit and durable receipts.
if(BUILD_TESTING)
  add_executable(galaxy_flight03_native_rendezvous_tests
    tests/FLIGHT03_NativeRendezvousTests.cpp)
  target_link_libraries(galaxy_flight03_native_rendezvous_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_flight03_native_rendezvous
    COMMAND galaxy_flight03_native_rendezvous_tests)
endif()

# FLIGHT-04B full native physical rendezvous to sealed freight and SQLite recovery.
if(BUILD_TESTING)
  add_test(NAME galaxy_flight04b_real_freight
    COMMAND galaxy_play13d_regional_facilities_tests --flight03-rendezvous-freight)
  set_tests_properties(galaxy_flight04b_real_freight PROPERTIES TIMEOUT 300)
endif()

# FLIGHT-05: physical NPC rendezvous + trusted ferry manifest rescue.
if(BUILD_TESTING)
  add_executable(galaxy_flight05_physical_rescue_tests
    tests/FLIGHT05_PhysicalRescueTests.cpp)
  target_link_libraries(galaxy_flight05_physical_rescue_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_flight05_physical_rescue
    COMMAND galaxy_flight05_physical_rescue_tests)
  set_tests_properties(galaxy_flight05_physical_rescue PROPERTIES TIMEOUT 300)
endif()

# FLIGHT-06: native verified rescue of actual guardians and dependent child.
if(BUILD_TESTING)
  add_executable(galaxy_flight06_dependent_rendezvous_tests
    tests/FLIGHT06_DependentRendezvousRescueTests.cpp)
  target_link_libraries(galaxy_flight06_dependent_rendezvous_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_flight06_dependent_rendezvous
    COMMAND galaxy_flight06_dependent_rendezvous_tests)
  set_tests_properties(galaxy_flight06_dependent_rendezvous PROPERTIES TIMEOUT 300)
endif()

# NPC-RESP-01: original NPC flight ledger as owner-response dispatcher.
if(BUILD_TESTING)
  add_executable(galaxy_npcresp01_dispatch_tests
    tests/NPCRESP01_DispatchTests.cpp)
  target_link_libraries(galaxy_npcresp01_dispatch_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_npcresp01_dispatch
    COMMAND galaxy_npcresp01_dispatch_tests)
  set_tests_properties(galaxy_npcresp01_dispatch PROPERTIES TIMEOUT 300)
endif()

# NPC-RESP-02: persistent player identity is not a ship or NPC.
if(BUILD_TESTING)
  add_executable(galaxy_npcresp02_player_identity_tests
    tests/NPCRESP02_PlayerIdentityTests.cpp)
  target_link_libraries(galaxy_npcresp02_player_identity_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_npcresp02_player_identity
    COMMAND galaxy_npcresp02_player_identity_tests)
  set_tests_properties(galaxy_npcresp02_player_identity PROPERTIES TIMEOUT 300)
endif()

# NPC-RESP-02B: original receipt + owner-consent player rescue native loop.
if(BUILD_TESTING)
  add_executable(galaxy_npcresp02b_player_rescue_tests
    tests/NPCRESP02B_PlayerRescueTests.cpp)
  target_link_libraries(galaxy_npcresp02b_player_rescue_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_npcresp02b_player_rescue
    COMMAND galaxy_npcresp02b_player_rescue_tests)
  set_tests_properties(galaxy_npcresp02b_player_rescue PROPERTIES TIMEOUT 300)
endif()

# NPC-RESP-03A: subjective hostile intent must never grant combat or arrest.
if(BUILD_TESTING)
  add_executable(galaxy_npcresp03_threat_policy_tests
    tests/NPCRESP03_ColonyThreatPolicyTests.cpp)
  target_link_libraries(galaxy_npcresp03_threat_policy_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_npcresp03_threat_policy
    COMMAND galaxy_npcresp03_threat_policy_tests)
endif()

# DISPOSITION-01: original governance/GRSM -> witnessed patrol, no combat.
if(BUILD_TESTING)
  add_executable(galaxy_disposition01_unified_response_tests
    tests/DISPOSITION01_UnifiedResponseTests.cpp)
  target_link_libraries(galaxy_disposition01_unified_response_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_disposition01_unified_response
    COMMAND galaxy_disposition01_unified_response_tests)
  set_tests_properties(galaxy_disposition01_unified_response PROPERTIES TIMEOUT 300)
endif()

# DISPOSITION-02: one historical SQLite journal; native territory and recovery tests.
if(BUILD_TESTING)
  add_executable(galaxy_disposition02_territory_tests
    tests/DISPOSITION02_TerritoryJournalTests.cpp)
  target_link_libraries(galaxy_disposition02_territory_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_disposition02_territory
    COMMAND galaxy_disposition02_territory_tests)
  set_tests_properties(galaxy_disposition02_territory PROPERTIES TIMEOUT 300)
endif()

# DISPOSITION-03: visitor-owned territorial notice receipts, grace and enforcement.
if(BUILD_TESTING)
  add_executable(galaxy_disposition03_territorial_comms_tests
    tests/DISPOSITION03_TerritorialCommsTests.cpp)
  target_link_libraries(galaxy_disposition03_territorial_comms_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_disposition03_territorial_comms
    COMMAND galaxy_disposition03_territorial_comms_tests)
  set_tests_properties(galaxy_disposition03_territorial_comms PROPERTIES TIMEOUT 300)
endif()

# DISPOSITION-03B: player hailing policy and lawful evidence-bound aggravation.
if(BUILD_TESTING)
  add_executable(galaxy_disposition03b_hailing_policy_tests
    tests/DISPOSITION03B_HailingPolicyTests.cpp)
  target_link_libraries(galaxy_disposition03b_hailing_policy_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_disposition03b_hailing_policy
    COMMAND galaxy_disposition03b_hailing_policy_tests)
  set_tests_properties(galaxy_disposition03b_hailing_policy PROPERTIES TIMEOUT 300)
endif()

# DISPOSITION-03C: controlled installation travel-clearance policy.
if(BUILD_TESTING)
  add_executable(galaxy_disposition03c_travel_clearance_tests
    tests/DISPOSITION03C_TravelClearanceTests.cpp)
  target_link_libraries(galaxy_disposition03c_travel_clearance_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_disposition03c_travel_clearance
    COMMAND galaxy_disposition03c_travel_clearance_tests)
  set_tests_properties(galaxy_disposition03c_travel_clearance PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-01: deterministic temporary module-control confusion prototype.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status01_confusion_tests
    tests/SHIPSTATUS01_ConfusionTests.cpp)
  target_link_libraries(galaxy_ship_status01_confusion_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status01_confusion
    COMMAND galaxy_ship_status01_confusion_tests)
  set_tests_properties(galaxy_ship_status01_confusion PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-02: native command gating and same-world SQLite/v100 recovery.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status02_authoritative_tests
    tests/SHIPSTATUS02_AuthoritativePersistenceTests.cpp)
  target_link_libraries(galaxy_ship_status02_authoritative_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status02_authoritative
    COMMAND galaxy_ship_status02_authoritative_tests)
  set_tests_properties(galaxy_ship_status02_authoritative PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-03: real fabricated emitter/firewall, native attack and recovery.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status03_physical_hacking_tests
    tests/SHIPSTATUS03_PhysicalHackingTests.cpp)
  target_link_libraries(galaxy_ship_status03_physical_hacking_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status03_physical_hacking
    COMMAND galaxy_ship_status03_physical_hacking_tests)
  set_tests_properties(galaxy_ship_status03_physical_hacking PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-04: original NPC cyber operator and territorial attack evidence.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status04_npc_cyber_tests
    tests/SHIPSTATUS04_NpcCyberTacticsTests.cpp)
  target_link_libraries(galaxy_ship_status04_npc_cyber_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status04_npc_cyber
    COMMAND galaxy_ship_status04_npc_cyber_tests)
  set_tests_properties(galaxy_ship_status04_npc_cyber PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-05: one native recurring original-world NPC supervisor.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status05_supervisor_tests
    tests/SHIPSTATUS05_SupervisorTests.cpp)
  target_link_libraries(galaxy_ship_status05_supervisor_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status05_supervisor
    COMMAND galaxy_ship_status05_supervisor_tests)
  set_tests_properties(galaxy_ship_status05_supervisor PROPERTIES TIMEOUT 300)
  find_program(SG_STATUS05_NODE NAMES node node.exe)
  if(SG_STATUS05_NODE)
    add_test(NAME galaxy_ship_status05_cockpit
      COMMAND ${SG_STATUS05_NODE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/SHIPSTATUS05_CyberCockpitTests.js)
  endif()
endif()

# SHIP-STATUS-06: same authoritative patrol flight, physical rendezvous,
# governed NPC cyber attack and committed player defense read model.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status06_fleet_cyber_tests
    tests/SHIPSTATUS06_FleetCyberTests.cpp)
  target_link_libraries(galaxy_ship_status06_fleet_cyber_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status06_fleet_cyber
    COMMAND galaxy_ship_status06_fleet_cyber_tests)
  set_tests_properties(galaxy_ship_status06_fleet_cyber PROPERTIES TIMEOUT 300)
endif()

if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME galaxy_ship_status06_live_cyber
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/SHIPSTATUS06_LiveCyberHostTests.py
        --host $<TARGET_FILE:galaxy_colony_live_host>
        --relay ${CMAKE_CURRENT_SOURCE_DIR}/live/interactive_relay.py
        --bridge $<TARGET_FILE:galaxy_shipping_status_bridge>)
    set_tests_properties(galaxy_ship_status06_live_cyber PROPERTIES TIMEOUT 90)
  endif()
endif()

# SHIP-STATUS-07: intake verified same-tick original cyber aggression as an
# immutable independent case. It has no crime/attack/arrest powers.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07_incident_tests
    tests/SHIPSTATUS07_CyberIncidentJournalTests.cpp)
  target_link_libraries(galaxy_ship_status07_incident_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07_incident
    COMMAND galaxy_ship_status07_incident_tests)
  set_tests_properties(galaxy_ship_status07_incident PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-07B: original material-backed stealth, acquired anonymous
# contacts, powered sensor hierarchy, and canonical v103 restart.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07_sensor_tests
    tests/SHIPSTATUS07_StealthSensorTests.cpp)
  target_link_libraries(galaxy_ship_status07_sensor_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07_sensors
    COMMAND galaxy_ship_status07_sensor_tests)
  set_tests_properties(galaxy_ship_status07_sensors PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-07C: scoped player graphical visibility, real native
# hide/reacquire deltas and foreign owner/cargo non-disclosure.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07_view_privacy_tests
    tests/SHIPSTATUS07_ViewPrivacyTests.cpp)
  target_link_libraries(galaxy_ship_status07_view_privacy_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07_view_privacy
    COMMAND galaxy_ship_status07_view_privacy_tests)
  set_tests_properties(galaxy_ship_status07_view_privacy PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-07D: native authenticated owner-scoped security desk, real
# voluntary territorial hails, prior cyber evidence, and SQLite recovery.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07d_security_desk_tests
    tests/SHIPSTATUS07D_SecurityDeskTests.cpp)
  target_link_libraries(galaxy_ship_status07d_security_desk_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07d_security_desk
    COMMAND galaxy_ship_status07d_security_desk_tests)
  set_tests_properties(galaxy_ship_status07d_security_desk PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-07E: immutable original ship-to-ship hail contacts, verified
# identity replies and nuisance escalation without fabricated police powers.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07e_hail_abuse_tests
    tests/SHIPSTATUS07E_HailAbuseTests.cpp)
  target_link_libraries(galaxy_ship_status07e_hail_abuse_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07e_hail_abuse
    COMMAND galaxy_ship_status07e_hail_abuse_tests)
  set_tests_properties(galaxy_ship_status07e_hail_abuse PROPERTIES TIMEOUT 300)
endif()

# STATUS-COMMS-01 / STATUS-NPC-01: original governance, real finite
# on-duty NPC operator, authenticated direct hail identity/decline/silence
# and durable restart. This is native original-game, not a mock world.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07f_npc_hail_tests
    tests/SHIPSTATUS07F_NpcHailSupervisorTests.cpp)
  target_link_libraries(galaxy_ship_status07f_npc_hail_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07f_npc_hail
    COMMAND galaxy_ship_status07f_npc_hail_tests)
  set_tests_properties(galaxy_ship_status07f_npc_hail PROPERTIES TIMEOUT 300)
endif()

# COMBAT-PHYSICAL-01A: original C++/SQLite physical ship-to-ship finite
# ammunition, real sensor/yaw/hull/wreck and protected crew custody v104.
if(BUILD_TESTING)
  add_executable(galaxy_combat01a_ship_weapon_tests
    tests/COMBAT01A_RealShipWeaponTests.cpp)
  target_link_libraries(galaxy_combat01a_ship_weapon_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_combat01a_ship_weapon
    COMMAND galaxy_combat01a_ship_weapon_tests)
  set_tests_properties(galaxy_combat01a_ship_weapon PROPERTIES TIMEOUT 300)
endif()

# STATUS-07G: two genuine human owner IDs in a deliberately seeded ORIGINAL
# test world, with no automatic production second ship or global SGVIEW leak.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07g_two_human_tests
    tests/SHIPSTATUS07G_TwoHumanSecurityTests.cpp)
  target_link_libraries(galaxy_ship_status07g_two_human_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07g_two_human
    COMMAND galaxy_ship_status07g_two_human_tests)
  set_tests_properties(galaxy_ship_status07g_two_human PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-07I: original same-transaction durable owner notification outbox
# with owner-scoped replay, idempotent ACK, adversarial rollback and restart.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07i_outbox_tests
    tests/SHIPSTATUS07I_DurableOutboxTests.cpp)
  target_link_libraries(galaxy_ship_status07i_outbox_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07i_outbox
    COMMAND galaxy_ship_status07i_outbox_tests)
  set_tests_properties(galaxy_ship_status07i_outbox PROPERTIES TIMEOUT 300)
endif()

# SHIP-STATUS-07J: original staffed government appointment and independently
# witnessed verified nuisance intake. No verdict, warrant, police or weapons.
if(BUILD_TESTING)
  add_executable(galaxy_ship_status07j_law_agency_tests
    tests/SHIPSTATUS07J_LawAgencyTests.cpp)
  target_link_libraries(galaxy_ship_status07j_law_agency_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_ship_status07j_law_agency
    COMMAND galaxy_ship_status07j_law_agency_tests)
  set_tests_properties(galaxy_ship_status07j_law_agency PROPERTIES TIMEOUT 300)
endif()

# INDUSTRY-01: source-generated candidate catalog linked into the SAME native
# game kernel; original-world workshop and SQLite semantics remain authoritative.
if(BUILD_TESTING)
    add_executable(galaxy_industry01_native_tests tests/INDUSTRY01_NativeTests.cpp)
    target_link_libraries(galaxy_industry01_native_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_industry01_native_binding COMMAND galaxy_industry01_native_tests)
endif()

# WO2: professions are a read-only generated mapping; real staffing uses the
# ORIGINAL PersistentWorld worker commands and SQLite work receipts.
if(BUILD_TESTING)
  add_executable(galaxy_wo2_industry_workforce_tests tests/WO2IndustryWorkforceTests.cpp)
  target_link_libraries(galaxy_wo2_industry_workforce_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_wo2_industry_workforce COMMAND galaxy_wo2_industry_workforce_tests)
endif()

# WO3-A: proposal-only colony construction planning and original-world actual
# facility, census, utility, immutable SQLite copies and material conservation.
if(BUILD_TESTING)
  add_executable(galaxy_wo3_colony_operations_tests tests/WO3ColonyOperationsTests.cpp)
  target_link_libraries(galaxy_wo3_colony_operations_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_wo3_colony_operations COMMAND galaxy_wo3_colony_operations_tests)
  set_tests_properties(galaxy_wo3_colony_operations PROPERTIES TIMEOUT 300)
  add_executable(galaxy_wo3_planetary_housing_tests tests/WO3PlanetaryHousingTests.cpp)
  target_link_libraries(galaxy_wo3_planetary_housing_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_wo3_planetary_housing COMMAND galaxy_wo3_planetary_housing_tests)
  set_tests_properties(galaxy_wo3_planetary_housing PROPERTIES TIMEOUT 300)
  add_executable(galaxy_wo3_planetary_utility_tests tests/WO3PlanetaryUtilityTests.cpp)
  target_link_libraries(galaxy_wo3_planetary_utility_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_wo3_planetary_utility COMMAND galaxy_wo3_planetary_utility_tests)
  set_tests_properties(galaxy_wo3_planetary_utility PROPERTIES TIMEOUT 300)
endif()


# WO4-A: owner-scoped scarcity forecast over original physical life support.
# No economy duplicate and no simulated distribution writer.
if(BUILD_TESTING)
    add_executable(galaxy_wo4_resource_demand_tests tests/WO4ResourceDemandTests.cpp)
    target_link_libraries(galaxy_wo4_resource_demand_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_wo4_resource_demand COMMAND galaxy_wo4_resource_demand_tests)
endif()


# WO4-C native industrial stockpile: existing material cargo -> constructed
# storage, original command journal, conservation and copied SQLite recovery.
if(BUILD_TESTING)
    add_executable(galaxy_wo4_material_allocation_tests tests/WO4MaterialAllocationTests.cpp)
    target_link_libraries(galaxy_wo4_material_allocation_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_wo4_material_allocation COMMAND galaxy_wo4_material_allocation_tests)
endif()


# WO5 A/B original physical shuttle/resupply and intercolony booking admission.
# The real arrival/SQLite recovery acceptance is exercised by the existing
# WO3 planetary utility integration target, without shadow vehicle state.
if(BUILD_TESTING)
    add_executable(galaxy_wo5_physical_logistics_tests tests/WO5PhysicalLogisticsTests.cpp)
    target_link_libraries(galaxy_wo5_physical_logistics_tests PRIVATE galaxy_kernel)
    add_test(NAME galaxy_wo5_physical_logistics COMMAND galaxy_wo5_physical_logistics_tests)
endif()


# SPACE-01/02/03: native, original-world 3D computational cell occupancy,
# exact SQLite atomic migration, per-cell worker authority and WO5 freight
# identity/recovery. No browser-only mock or second material ledger.
if(BUILD_TESTING)
  add_executable(galaxy_seamless_spatial_ownership_tests
      tests/SeamlessSpatialOwnershipTests.cpp)
  target_link_libraries(galaxy_seamless_spatial_ownership_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_seamless_spatial_ownership
      COMMAND galaxy_seamless_spatial_ownership_tests)
endif()

# SPACE-04B: exact 3D floating-origin geometry (source foundation, not live flight).
if(BUILD_TESTING)
  add_executable(galaxy_floating_frame_tests tests/GalacticFloatingFrameTests.cpp)
  target_link_libraries(galaxy_floating_frame_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_floating_frame COMMAND galaxy_floating_frame_tests)
endif()

# SPACE-04B/05: actual original v119 flight and fenced SQLite world steps.
if(BUILD_TESTING)
  add_executable(galaxy_native_3d_worker_tests tests/Native3DPhysicsWorkerTests.cpp)
  target_link_libraries(galaxy_native_3d_worker_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_native_3d_worker COMMAND galaxy_native_3d_worker_tests)
endif()

find_package(Threads REQUIRED)
target_link_libraries(galaxy_kernel PUBLIC Threads::Threads)

# SPACE-04E: independent signed 128-bit native property oracle.
# Windows shipping physics intentionally does NOT depend on __int128.
if(BUILD_TESTING AND NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_executable(galaxy_high_order_math_property_tests
    tests/NativeHighOrderMathPropertyTests.cpp)
  target_include_directories(galaxy_high_order_math_property_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME galaxy_high_order_math_property
    COMMAND galaxy_high_order_math_property_tests)
endif()

# SPACE-05C: native subprocess never receives an original-world writer.
add_executable(galaxy_native_cell_worker src/native_cell_worker.cpp)
if(BUILD_TESTING)
  add_executable(galaxy_native_cell_hang_worker tests/NativeCellHangWorker.cpp)
  target_link_libraries(galaxy_native_cell_hang_worker PRIVATE Threads::Threads)
  add_executable(galaxy_native_cell_detached_pipe_worker
    tests/NativeCellDetachedPipeWorker.cpp)
  add_executable(galaxy_native_cell_fork_fence_worker
    tests/NativeCellForkFenceWorker.cpp)
endif()

# SPACE-05C: independent 4x20 native process admission endurance and
# deliberately hung-child process-tree timeout acceptance, no game DB.
if(BUILD_TESTING)
  add_executable(galaxy_native_cell_process_stress_tests
    tests/NativeCellProcessStressTests.cpp)
  target_link_libraries(galaxy_native_cell_process_stress_tests
    PRIVATE galaxy_kernel Threads::Threads)
  add_test(NAME galaxy_native_cell_process_stress
    COMMAND galaxy_native_cell_process_stress_tests
      $<TARGET_FILE:galaxy_native_cell_worker>
      $<TARGET_FILE:galaxy_native_cell_hang_worker>
      $<TARGET_FILE:galaxy_native_cell_detached_pipe_worker>
      $<TARGET_FILE:galaxy_native_cell_fork_fence_worker>)
  # Extended real-process endurance: 4 physical cell lanes x 1000 starts.
  # Same original worker source and timeout/containment assertions.
  add_test(NAME galaxy_native_cell_process_long_stress
    COMMAND galaxy_native_cell_process_stress_tests
      $<TARGET_FILE:galaxy_native_cell_worker>
      $<TARGET_FILE:galaxy_native_cell_hang_worker>
      $<TARGET_FILE:galaxy_native_cell_detached_pipe_worker>
      $<TARGET_FILE:galaxy_native_cell_fork_fence_worker>
      1000)
  set_tests_properties(galaxy_native_cell_process_long_stress
    PROPERTIES TIMEOUT 120)
endif()

# SPACE-04C/04D/05B: native high-order coordinates, 3D probes and bounded
# active multi-cell original-world supervisor. Runs on Windows and Linux.
# SPACE-05J: opt-in 1,024 original-world SQLite ticks, not a component mock.
option(SPACE_ENABLE_1024_ENDURANCE
  "Register prolonged 1,024-tick four-cell original-world native worker acceptance" OFF)
if(BUILD_TESTING)
  add_executable(galaxy_native_galactic_supervisor_tests
    tests/NativeGalacticProbeSupervisorTests.cpp)
  target_link_libraries(galaxy_native_galactic_supervisor_tests PRIVATE galaxy_kernel Threads::Threads)
  add_test(NAME galaxy_native_galactic_supervisor
           COMMAND galaxy_native_galactic_supervisor_tests
             $<TARGET_FILE:galaxy_native_cell_worker>
             $<TARGET_FILE:galaxy_native_cell_hang_worker>)
  if(SPACE_ENABLE_1024_ENDURANCE)
    add_test(NAME galaxy_native_galactic_supervisor_1024
             COMMAND galaxy_native_galactic_supervisor_tests
               $<TARGET_FILE:galaxy_native_cell_worker>
               $<TARGET_FILE:galaxy_native_cell_hang_worker>)
    set_tests_properties(galaxy_native_galactic_supervisor_1024 PROPERTIES
      ENVIRONMENT "SPACE_ENDURANCE_TICKS=1024"
      TIMEOUT 900
      LABELS "space;original-world;endurance;sqlite")
  endif()
endif()

# SGDX-01: private host-incident evidence is independently testable without
# constructing a second world or modifying the authoritative SQLite writer.
if(BUILD_TESTING)
  add_executable(galaxy_server_diagnostics_tests tests/ServerDiagnosticsTests.cpp)
  target_include_directories(galaxy_server_diagnostics_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME galaxy_server_diagnostics COMMAND galaxy_server_diagnostics_tests)
endif()

# SGDX-01 actual native-host catch path, distinct from the isolated component.
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
  add_test(NAME galaxy_server_diagnostics_native_hosts
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/SGDX01HostExceptionTests.py
      $<TARGET_FILE:galaxy_colony_live_host>
      $<TARGET_FILE:galaxy_interactive_host>)
  set_tests_properties(galaxy_server_diagnostics_native_hosts
    PROPERTIES TIMEOUT 40)
endif()

# SGDX-02: independent native host/worker supervisor. It never links to or
# mutates the authoritative game kernel; game stdout is forwarded unchanged.
add_executable(galaxy_server_dx_supervisor src/server_dx_supervisor.cpp)
target_include_directories(galaxy_server_dx_supervisor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WIN32)
  target_link_libraries(galaxy_server_dx_supervisor PRIVATE Dbghelp Advapi32)
endif()
if(BUILD_TESTING)
  add_executable(galaxy_server_dx_fault_child tests/ServerDx02FaultChild.cpp)
  target_include_directories(galaxy_server_dx_fault_child PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(galaxy_server_dx_fault_child PRIVATE Threads::Threads)
  if(Python3_Interpreter_FOUND)
    add_test(NAME galaxy_server_dx_supervisor
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/SGDX02SupervisorTests.py
        $<TARGET_FILE:galaxy_server_dx_supervisor>
        $<TARGET_FILE:galaxy_server_dx_fault_child>)
    set_tests_properties(galaxy_server_dx_supervisor PROPERTIES TIMEOUT 120)

    add_test(NAME galaxy_server_dx_sqlite_recovery
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/SGDX02SqliteRecoveryTests.py
        $<TARGET_FILE:galaxy_server_dx_supervisor>)
    set_tests_properties(galaxy_server_dx_sqlite_recovery PROPERTIES TIMEOUT 90)
    add_test(NAME galaxy_server_dx_game_hosts
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/SGDX02GameHostTests.py
        $<TARGET_FILE:galaxy_server_dx_supervisor>
        $<TARGET_FILE:galaxy_colony_live_host>
        $<TARGET_FILE:galaxy_interactive_host>
        $<TARGET_FILE:galaxy_native_cell_worker>)
    set_tests_properties(galaxy_server_dx_game_hosts PROPERTIES TIMEOUT 240)
  endif()
endif()

# SGDX-02 Windows NTFS incident ACLs are used in native game-host and
# original worker-diagnostic paths, not only by the external dump utility.
if(WIN32)
  foreach(dx_target IN ITEMS
      galaxy_kernel galaxy_colony_live_host galaxy_interactive_host
      galaxy_server_diagnostics_tests galaxy_server_dx_fault_child)
    if(TARGET ${dx_target})
      target_link_libraries(${dx_target} PRIVATE Advapi32)
    endif()
  endforeach()
endif()

# SPACE-05M: exact-source snapshot upgrade and multi-star genesis boundary.
if(BUILD_TESTING)
  add_executable(galaxy_space05m_upgrade_boundaries_tests
    tests/SPACE05M_SnapshotMigrationGenesisTests.cpp)
  target_link_libraries(galaxy_space05m_upgrade_boundaries_tests PRIVATE galaxy_kernel)
  add_test(NAME galaxy_space05m_upgrade_boundaries
    COMMAND galaxy_space05m_upgrade_boundaries_tests)
endif()
