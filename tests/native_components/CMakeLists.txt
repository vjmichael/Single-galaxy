cmake_minimum_required(VERSION 3.20)
# Independent native acceptance for the real SPACE worker source. Unlike
# the complete kernel CMake build, this isolated target needs no SQLite SDK.
# Use it when GitHub runners are unavailable; it never proves physical
# multi-cell gameplay ownership, freight, or installed Windows acceptance.
project(single_galaxy_space_native_components LANGUAGES CXX)
include(CTest)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(SG_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(space_native_cell_runner STATIC
  "${SG_SOURCE_ROOT}/src/NativeCellProcessRunner.cpp")
target_include_directories(space_native_cell_runner PUBLIC
  "${SG_SOURCE_ROOT}/include")
target_link_libraries(space_native_cell_runner PUBLIC Threads::Threads)

add_executable(galaxy_native_cell_worker
  "${SG_SOURCE_ROOT}/src/native_cell_worker.cpp")
add_executable(galaxy_native_cell_hang_worker
  "${SG_SOURCE_ROOT}/tests/NativeCellHangWorker.cpp")
add_executable(galaxy_native_cell_detached_pipe_worker
  "${SG_SOURCE_ROOT}/tests/NativeCellDetachedPipeWorker.cpp")
add_executable(galaxy_native_cell_fork_fence_worker
  "${SG_SOURCE_ROOT}/tests/NativeCellForkFenceWorker.cpp")
add_executable(galaxy_native_cell_process_stress_tests
  "${SG_SOURCE_ROOT}/tests/NativeCellProcessStressTests.cpp")
target_link_libraries(galaxy_native_cell_process_stress_tests
  PRIVATE space_native_cell_runner)

if(BUILD_TESTING)
  add_test(NAME galaxy_native_cell_process_stress
    COMMAND galaxy_native_cell_process_stress_tests
      $<TARGET_FILE:galaxy_native_cell_worker>
      $<TARGET_FILE:galaxy_native_cell_hang_worker>
      $<TARGET_FILE:galaxy_native_cell_detached_pipe_worker>
      $<TARGET_FILE:galaxy_native_cell_fork_fence_worker>)
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

# GCC/Clang independent 128-bit oracle is deliberately not MSVC-compatible;
# the shipping portable high-order math itself is MSVC compatible.
if(NOT MSVC)
  add_executable(galaxy_high_order_math_property_tests
    "${SG_SOURCE_ROOT}/tests/NativeHighOrderMathPropertyTests.cpp")
  target_include_directories(galaxy_high_order_math_property_tests PRIVATE
    "${SG_SOURCE_ROOT}/include")
  if(BUILD_TESTING)
    add_test(NAME galaxy_high_order_math_property
      COMMAND galaxy_high_order_math_property_tests)
  endif()
endif()
