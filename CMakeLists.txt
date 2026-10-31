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
    src/ColonyWaterSupply.cpp src/Persistence.cpp src/ClientContract.cpp src/Galaxy.cpp src/Navigation.cpp src/GateTransit.cpp src/Region.cpp)
target_include_directories(galaxy_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(galaxy_kernel PUBLIC SQLite::SQLite3)
if(MSVC)
    target_compile_options(galaxy_kernel PRIVATE /W4 /permissive-)
else()
    target_compile_options(galaxy_kernel PRIVATE -Wall -Wextra -Wpedantic)
endif()
include(CTest)
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
