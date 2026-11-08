cmake_minimum_required(VERSION 3.18)
project(SingleGalaxySGDX02 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(galaxy_server_dx_supervisor
  "${GAME_ROOT}/src/server_dx_supervisor.cpp")
target_include_directories(galaxy_server_dx_supervisor PRIVATE "${GAME_ROOT}/include")
if(WIN32)
  target_link_libraries(galaxy_server_dx_supervisor PRIVATE Dbghelp Advapi32)
endif()
add_executable(galaxy_server_dx_fault_child
  "${GAME_ROOT}/tests/ServerDx02FaultChild.cpp")
target_include_directories(galaxy_server_dx_fault_child PRIVATE "${GAME_ROOT}/include")
find_package(Threads REQUIRED)
target_link_libraries(galaxy_server_dx_fault_child PRIVATE Threads::Threads)
if(NOT MSVC)
  target_compile_options(galaxy_server_dx_supervisor PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(galaxy_server_dx_fault_child PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME sgdx02_supervisor COMMAND ${Python3_EXECUTABLE}
  "${GAME_ROOT}/tests/SGDX02SupervisorTests.py"
  $<TARGET_FILE:galaxy_server_dx_supervisor>
  $<TARGET_FILE:galaxy_server_dx_fault_child>)
set_tests_properties(sgdx02_supervisor PROPERTIES TIMEOUT 90)
