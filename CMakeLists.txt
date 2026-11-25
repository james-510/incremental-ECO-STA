cmake_minimum_required(VERSION 3.20)
project(IncrementalECO_STA LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sta_timing src/sta/timing_engine.cpp)
target_include_directories(sta_timing PUBLIC include)
add_library(sta_paths src/paths/path_enumerator.cpp)
target_link_libraries(sta_paths PUBLIC sta_timing)
add_library(eco src/eco/eco_manager.cpp)
target_include_directories(eco PUBLIC include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(target sta_timing sta_paths eco)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endforeach()
endif()

# Current CLI: hand-built fixed-delay fixtures, not a netlist CLI.
add_library(sta_samples STATIC tests/fixtures/samples.cpp)
target_include_directories(sta_samples PUBLIC tests/fixtures)
target_link_libraries(sta_samples PUBLIC sta_paths)
add_executable(sta_demo src/app/main.cpp)
target_link_libraries(sta_demo PRIVATE sta_samples)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_timing tests/unit/test_timing.cpp)
  target_link_libraries(test_timing PRIVATE sta_samples)
  add_test(NAME timing_unit COMMAND test_timing)
  foreach(sample budget_compare violations missing_boundaries ties)
    add_test(NAME timing_${sample}
      COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:sta_demo>
        -DSAMPLE=${sample}
        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/expected/${sample}.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_sample.cmake)
  endforeach()
  add_test(NAME timing_cycle
    COMMAND ${CMAKE_COMMAND}
      -DPROGRAM=$<TARGET_FILE:sta_demo> -DSAMPLE=cycle
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_sample.cmake)

  foreach(program test_eco_manager test_eco_designs eco_demo)
    add_executable(${program} tests/eco/${program}.cpp)
    target_link_libraries(${program} PRIVATE eco)
    target_compile_definitions(${program} PRIVATE ECO_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  endforeach()
  add_test(NAME eco_manager COMMAND test_eco_manager)
  add_test(NAME eco_designs COMMAND test_eco_designs)
  add_test(NAME eco_demo_c17 COMMAND eco_demo
    --lib ${CMAKE_CURRENT_SOURCE_DIR}/data/lib/mini_sky130.lib
    --netlist ${CMAKE_CURRENT_SOURCE_DIR}/data/netlists/c17.v
    --eco ${CMAKE_CURRENT_SOURCE_DIR}/data/eco/c17_edits.txt --period 1.0 --undo)
  add_test(NAME eco_demo_generated COMMAND eco_demo
    --lib ${CMAKE_CURRENT_SOURCE_DIR}/data/lib/mini_sky130.lib --gen 20,100,7,5
    --eco ${CMAKE_CURRENT_SOURCE_DIR}/data/eco/gen_20x100_edits.txt --period 3.0 --undo)
  set_tests_properties(eco_demo_c17 eco_demo_generated PROPERTIES FAIL_REGULAR_EXPRESSION "MISMATCH|FAILED")
endif()
