cmake_minimum_required(VERSION 3.18)
project(SingleGalaxySGDX02 LANGUAGES CXX)
option(SGDX_REQUIRE_NATIVE_MSVC "Refuse cross-build/surrogate compiler for Windows acceptance" OFF)
if(SGDX_REQUIRE_NATIVE_MSVC)
  if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "SGDX supported-Windows acceptance requires native MSVC, not clang-cl or Linux")
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SGDX Windows acceptance must build x64, not Win32")
  endif()
  message(STATUS "SGDX Windows compiler accepted: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, ${CMAKE_CXX_COMPILER}, x64")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(galaxy_server_dx_supervisor
  "${GAME_ROOT}/src/server_dx_supervisor.cpp")
target_include_directories(galaxy_server_dx_supervisor PRIVATE "${GAME_ROOT}/include")
if(WIN32)
  target_link_libraries(galaxy_server_dx_supervisor PRIVATE dbghelp advapi32)
endif()
add_executable(galaxy_server_dx_fault_child
  "${GAME_ROOT}/tests/ServerDx02FaultChild.cpp")
target_include_directories(galaxy_server_dx_fault_child PRIVATE "${GAME_ROOT}/include")
if(WIN32)
  # Windows uses CRT threads; a Linux-hosted cross-build must not add -lpthreads.
  target_link_libraries(galaxy_server_dx_fault_child PRIVATE advapi32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(galaxy_server_dx_fault_child PRIVATE Threads::Threads)
endif()
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

add_test(NAME sgdx02_sqlite_recovery COMMAND ${Python3_EXECUTABLE}
  "${GAME_ROOT}/tests/SGDX02SqliteRecoveryTests.py"
  $<TARGET_FILE:galaxy_server_dx_supervisor>)
set_tests_properties(sgdx02_sqlite_recovery PROPERTIES TIMEOUT 90)
