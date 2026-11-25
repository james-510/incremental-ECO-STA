CXX ?= c++
CXXFLAGS ?= -std=c++17 -O0 -g -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude -Itests/fixtures -Itests/eco

TIMING_SRC = src/sta/timing_engine.cpp src/paths/path_enumerator.cpp
SAMPLES_SRC = tests/fixtures/samples.cpp
TIMING_HEADERS = $(wildcard include/graph/*.hpp include/sta/*.hpp include/paths/*.hpp tests/fixtures/*.hpp)
ECO_SRC = src/eco/eco_manager.cpp
ECO_HEADERS = $(wildcard include/common/*.hpp include/eco/*.hpp tests/eco/*.hpp)
PROGRAMS = build/sta_demo build/test_timing build/test_eco_manager build/test_eco_designs build/eco_demo

.PHONY: all test clean
all: $(PROGRAMS)

build:
	mkdir -p $@

build/sta_demo: $(TIMING_SRC) $(SAMPLES_SRC) src/app/main.cpp $(TIMING_HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TIMING_SRC) $(SAMPLES_SRC) src/app/main.cpp -o $@

build/test_timing: $(TIMING_SRC) $(SAMPLES_SRC) tests/unit/test_timing.cpp $(TIMING_HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TIMING_SRC) $(SAMPLES_SRC) tests/unit/test_timing.cpp -o $@

build/test_eco_manager: $(ECO_SRC) tests/eco/test_eco_manager.cpp $(ECO_HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ECO_SRC) tests/eco/test_eco_manager.cpp -o $@

build/test_eco_designs: $(ECO_SRC) tests/eco/test_eco_designs.cpp $(ECO_HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ECO_SRC) tests/eco/test_eco_designs.cpp -o $@

build/eco_demo: $(ECO_SRC) tests/eco/eco_demo.cpp $(ECO_HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ECO_SRC) tests/eco/eco_demo.cpp -o $@

test: all
	./build/test_timing
	./build/sta_demo budget_compare | diff -u tests/fixtures/expected/budget_compare.txt -
	./build/sta_demo violations | diff -u tests/fixtures/expected/violations.txt -
	./build/sta_demo missing_boundaries | diff -u tests/fixtures/expected/missing_boundaries.txt -
	./build/sta_demo ties | diff -u tests/fixtures/expected/ties.txt -
	./build/sta_demo cycle > build/cycle.log 2>&1; code=$$?; test $$code -eq 1 && grep -q 'combinational cycle detected' build/cycle.log
	./build/test_eco_manager
	ECO_DATA_DIR="$(CURDIR)/data" ./build/test_eco_designs
	./build/eco_demo --lib data/lib/mini_sky130.lib --netlist data/netlists/c17.v --eco data/eco/c17_edits.txt --period 1.0 --undo
	./build/eco_demo --lib data/lib/mini_sky130.lib --gen 20,100,7,5 --eco data/eco/gen_20x100_edits.txt --period 3.0 --undo

clean:
	$(RM) $(PROGRAMS) build/cycle.log
