cmake_minimum_required(VERSION 3.20)
project(Incremental-ECO-STA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(eco src/eco/eco_manager.cpp)
target_include_directories(eco PUBLIC include)
target_compile_options(eco PRIVATE -Wall -Wextra)

enable_testing()
# data/ is found via ECO_DATA_DIR (compile-time default below, env var overrides)
set(ECO_DATA "${CMAKE_SOURCE_DIR}/data")

add_executable(test_eco_manager tests/eco/test_eco_manager.cpp)
target_link_libraries(test_eco_manager eco)
add_test(NAME eco_manager COMMAND test_eco_manager)

add_executable(test_eco_designs tests/eco/test_eco_designs.cpp)
target_link_libraries(test_eco_designs eco)
target_compile_definitions(test_eco_designs PRIVATE ECO_DATA_DIR="${ECO_DATA}")
add_test(NAME eco_designs COMMAND test_eco_designs)

add_executable(eco_demo tests/eco/eco_demo.cpp)
target_link_libraries(eco_demo eco)
target_compile_definitions(eco_demo PRIVATE ECO_DATA_DIR="${ECO_DATA}")
add_test(NAME eco_demo_c17
        COMMAND eco_demo
                --lib     ${ECO_DATA}/lib/mini_sky130.lib
                --netlist ${ECO_DATA}/netlists/c17.v
                --eco     ${ECO_DATA}/eco/c17_edits.txt
                --period 1.0 --undo)

add_test(NAME eco_demo_generated
        COMMAND eco_demo
                --lib ${ECO_DATA}/lib/mini_sky130.lib
                --gen 20,100,7,5
                --eco ${ECO_DATA}/eco/gen_20x100_edits.txt
                --period 3.0 --undo)

set_tests_properties(eco_demo_c17 eco_demo_generated PROPERTIES 
                    FAIL_REGULAR_EXPRESSION "MISMATCH|FAILED")