cmake_minimum_required(VERSION 3.20)
project(Incremental-ECO-STA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(eco src/eco/eco_manager.cpp)
target_include_directories(eco PUBLIC include)
target_compile_options(eco PRIVATE -Wall -Wextra)

enable_testing()
add_executable(test_eco_manager tests/eco/test_eco_manager.cpp)
target_link_libraries(test_eco_manager eco)
add_test(NAME eco_manager COMMAND test_eco_manager)