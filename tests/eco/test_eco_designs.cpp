// Design-level tests: real library cells, parsed netlists, generated designs, and an independent
// reference timer that checks the DirtySeeds contract (incremental == full after every ECO).
//
//   ./test_eco_designs            quick run (a few seconds)
//   ./test_eco_designs --stress   bigger designs / more edits
//   ECO_DATA_DIR=/path/to/data    override where lib/ and netlists/ live
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include "eco/eco_manager.hpp"
#include "mock_backend.hpp"
#include "netlist_loader.hpp"
#include "design_gen.hpp"
#include "ref_timer.hpp"

using namespace eco;
using U = std::uint32_t;
static const U kNil = 0xFFFFFFFFu;

static int g_fail = 0, g_checks = 0, g_tests = 0, g_tests_passed = 0;
static bool g_stress = false;
static std::vector<std::string> g_failed_tests;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #c "\n"; } } while (0)
#define CHECK_MATCH(m, t) do { std::string why_; ++g_checks; \
  if (!matchesFull(m, t, &why_)) { ++g_fail; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  incremental != full: " << why_ << "\n"; } } while (0)

static void runTest(const char* name, void (*fn)()) {
  int f0 = g_fail, c0 = g_checks;
  auto t0 = std::chrono::steady_clock::now();
  fn();
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  int checks = g_checks - c0, failed = g_fail - f0;
  ++g_tests;
  if (failed == 0) ++g_tests_passed; else g_failed_tests.push_back(name);
  std::cout << (failed == 0 ? "[  PASS  ] " : "[  FAIL  ] ") << name << "  (" << (checks - failed) << "/" << checks
            << " checks, " << static_cast<long>(ms) << " ms)\n";
}
#define RUN(t) runTest(#t, t)

// ------------------------------------------------------------------ helpers
static std::string dataDir() {
  if (const char* e = std::getenv("ECO_DATA_DIR")) return e;
#ifdef ECO_DATA_DIR
  return ECO_DATA_DIR;
#else
  return "data";
#endif
}
// ECO_LIB=/path/to/sky130_fd_sc_hd__tt_025C_1v80.lib runs every design test on the full 428-cell library.
static void loadLib(MockBackend& m) {
  if (const char* l = std::getenv("ECO_LIB")) loadLiberty(l, m); else loadLiberty(dataDir() + "/lib/mini_sky130.lib", m);
}
static LoadStats loadFile(MockBackend& m, const std::string& v) { loadLib(m); return loadVerilog(dataDir() + "/netlists/" + v, m); }
static LoadStats loadText(MockBackend& m, const std::string& v) { loadLib(m); return loadVerilogText(v, m); }
template <class F> static bool throws(F f) { try { f(); } catch (const std::exception&) { return true; } return false; }

static void applySeeds(RefTimer& t, ECOManager& e) {
  DirtySeeds s = e.takeSeeds();
  t.incrementalUpdate(s.arrival, s.requirement, s.structural);
}
static bool matchesFull(const MockBackend& m, const RefTimer& t, std::string* why = nullptr) {
  RefTimer f(m);
  f.fullUpdate();
  return t.sameAs(f, why);
}

static EcoResult randomEdit(MockBackend& m, ECOManager& e, std::mt19937& rng) {
  auto pick = [&](const std::vector<std::string>& v) -> const std::string& { return v[rng() % v.size()]; };
  switch (rng() % 7) {
    case 0: case 1: {
      auto insts = m.aliveInstNames();
      const std::string& i = pick(insts);
      auto c = m.swapCandidates(i);
      return c.empty() ? EcoResult{} : e.swapCell(i, pick(c));
    }
    case 2: {
      std::string net = pick(m.aliveNetNames());
      auto sinks = m.sinkPinNames(net);
      std::vector<std::string> subset;
      for (auto& s : sinks) if (rng() & 1) subset.push_back(s);
      auto bufs = m.bufferCellNames();
      return bufs.empty() ? EcoResult{} : e.insertBuffer(net, pick(bufs), subset);
    }
    case 3: { auto b = m.bufferInstNames(); return b.empty() ? EcoResult{} : e.removeBuffer(pick(b)); }
    case 4: case 5: {
      auto pins = m.inputPinNames(pick(m.aliveInstNames()));
      return pins.empty() ? EcoResult{} : e.reconnect(pick(pins), pick(m.aliveNetNames()));
    }
    default: return e.setClockPeriod(0.5 + (rng() % 40) / 10.0);
  }
}

// ------------------------------------------------------------------ loader tests
static void testLoadLiberty() {
  MockBackend m;
  std::size_t n = loadLiberty(dataDir() + "/lib/mini_sky130.lib", m);
  CHECK(n == 33);  // 11 cell types x 3 drive strengths
  U ff = m.findCell("sky130_fd_sc_hd__dfxtp_2"), nand = m.findCell("sky130_fd_sc_hd__nand2_4"), buf = m.findCell("sky130_fd_sc_hd__buf_1");
  CHECK(ff != kNil && nand != kNil && buf != kNil);
  CHECK(m.isSequentialCell(ff) && !m.isSequentialCell(nand));
  CHECK(m.isBufferCell(buf) && !m.isBufferCell(nand));
  CHECK(m.swapCompatible(m.findCell("sky130_fd_sc_hd__nand2_1"), nand));            // same footprint
  CHECK(!m.swapCompatible(nand, m.findCell("sky130_fd_sc_hd__nor2_1")));            // different function
  CHECK(m.cellDef(nand).in == (std::vector<std::string>{"A", "B"}) && m.cellDef(nand).out == (std::vector<std::string>{"Y"}));
  CHECK(m.cellDef(ff).in == (std::vector<std::string>{"CLK", "D"}));                // pg_pin ignored, sorted
}

static void testLoadC17() {
  MockBackend m;
  LoadStats st = loadFile(m, "c17.v");
  CHECK(st.module == "c17" && st.insts == 6 && st.inputs == 5 && st.outputs == 2);
  CHECK(st.nets == 11);
  CHECK(m.sinkPinNames("N11").size() == 2 && m.sinkPinNames("N16").size() == 2 && m.sinkPinNames("N3").size() == 2);
  CHECK(m.invariants().empty() && m.acyclic());
  CHECK(m.findPin("g19/A") != kNil && m.netOf(m.findPin("g19/A")) == m.findNet("N11"));
}

static void testLoadYosysStyle() {
  MockBackend m;
  LoadStats st = loadFile(m, "yosys_style_sample.v");
  CHECK(st.module == "top" && st.insts == 7 && st.inputs == 5 && st.outputs == 2 && st.constants == 1);
  CHECK(m.findInst("q_reg[0]") != kNil && m.findInst("q_reg[1]") != kNil);          // escaped instance names
  CHECK(m.findNet("a[1]") != kNil && m.findNet("y[0]") != kNil);                    // bus bits
  CHECK(m.sinkPinNames("a[0]").size() == 2);                                        // \a[0] and a[0] are the same net
  CHECK(m.findNet("__const1") != kNil && m.netOf(m.findPin("_07_/B")) == m.findNet("__const1"));
  CHECK(m.cellDef(m.cellOf(m.findInst("q_reg[0]"))).seq);
  CHECK(m.invariants().empty() && m.acyclic());
}

static void testLoadErrors() {
  auto bad = [](const std::string& body) {
    return throws([&] { MockBackend m; loadLib(m); loadVerilogText("module t (a, y);\n input a;\n output y;\n" + body + "\nendmodule\n", m); });
  };
  CHECK(bad("  nosuch_cell u0 (.A(a), .Y(y));"));                                       // unknown cell
  CHECK(bad("  sky130_fd_sc_hd__inv_1 u0 (.Q(a), .Y(y));"));                            // unknown pin
  CHECK(bad("  assign y = a;"));                                                         // unsupported
  CHECK(bad("  sky130_fd_sc_hd__inv_1 u0 (.A(a), .Y(y));\n  sky130_fd_sc_hd__inv_1 u0 (.A(a), .Y(y));"));  // duplicate inst
  CHECK(bad("  sky130_fd_sc_hd__inv_1 u0 (.A(a), .Y(y));\n  sky130_fd_sc_hd__inv_1 u1 (.A(a), .Y(y));"));  // two drivers
  CHECK(bad("  sky130_fd_sc_hd__inv_1 u0 (.A({a,a}), .Y(y));"));                         // concatenation
  CHECK(throws([] { MockBackend m; loadLib(m); loadVerilogText("module t (a);\n input a;\n", m); }));   // no endmodule
  CHECK(throws([] { MockBackend m; loadLib(m); loadVerilogText("module t (input a);\nendmodule\n", m); }));  // ANSI header
  CHECK(throws([] { MockBackend m; loadVerilog("/no/such/file.v", m); }));
}

// ------------------------------------------------------------------ reference timer sanity
static void testRefTimerInvariants() {
  MockBackend m; loadFile(m, "c17.v"); m.setClockPeriod(1.0);
  RefTimer t(m); t.fullUpdate();
  for (U i : m.aliveInsts()) {
    const auto& d = m.cellDef(m.cellOf(i));
    if (d.port || d.seq) continue;
    U out = m.outputPinsOf(i)[0];
    double minSlackIn = RefTimer::kInf;
    for (U in : m.inputPinsOf(i)) {
      CHECK(t.slack(in) >= t.slack(out) - 1e-12);                 // an input is never worse than... the output it feeds
      CHECK(t.arrival(out) >= t.arrival(in));                     // delay is positive
      minSlackIn = std::min(minSlackIn, t.slack(in));
    }
    CHECK(std::abs(minSlackIn - t.slack(out)) < 1e-12);           // the critical input carries the output's slack
  }
  U ep; double w = t.worstSlack(&ep);
  CHECK(ep != kNil && w < RefTimer::kInf);
  auto path = t.criticalPath();
  CHECK(path.size() >= 4 && !m.isOutputPin(path.back()) && m.cellDef(m.cellOf(m.instOf(path.front()))).port);
}

// ------------------------------------------------------------------ directed ECOs on c17
static void testC17EveryEdit() {
  MockBackend m; loadFile(m, "c17.v"); m.setClockPeriod(1.0);
  ECOManager e(m); RefTimer t(m); t.fullUpdate();
  RefTimer original(m); original.fullUpdate();
  std::string snap = m.snapshot();
  for (const char* g : {"g10", "g11", "g16", "g19", "g22", "g23"})
    for (const char* c : {"sky130_fd_sc_hd__nand2_2", "sky130_fd_sc_hd__nand2_4", "sky130_fd_sc_hd__nand2_1"}) {
      CHECK(e.swapCell(g, c).ok());
      applySeeds(t, e); CHECK_MATCH(m, t);
    }
  CHECK(t.sameAs(original));                                       // back to nand2_1 everywhere
  // buffer every net, one at a time, and take it back out
  for (const std::string& net : m.aliveNetNames()) {
    if (net.rfind("__", 0) == 0) continue;
    EcoResult r = e.insertBuffer(net, "sky130_fd_sc_hd__buf_2");
    if (!r.ok()) continue;
    applySeeds(t, e); CHECK_MATCH(m, t);
    auto removed = e.removeBuffer(r.created);
    if (removed.ok()) { applySeeds(t, e); CHECK_MATCH(m, t); }
    else CHECK(removed.status == EcoStatus::PortNetProtected);     // net feeding output port N22/N23
  }
  e.undoAll(); applySeeds(t, e);
  CHECK(m.snapshot() == snap);
  CHECK_MATCH(m, t);
  CHECK(t.sameAs(original));
}

static void testC17Reconnect() {
  MockBackend m; loadFile(m, "c17.v"); m.setClockPeriod(1.0);
  ECOManager e(m); RefTimer t(m); t.fullUpdate();
  CHECK(e.reconnect("g22/A", "N16").ok());                          // both inputs of g22 now on N16
  applySeeds(t, e); CHECK_MATCH(m, t);
  CHECK(e.reconnect("g16/B", "N22").status == EcoStatus::WouldCreateCycle);  // g16 -> g22 -> g16
  CHECK(e.reconnect("g11/A", "N23").status == EcoStatus::WouldCreateCycle);  // g11 -> g19 -> g23 -> g11
  CHECK(e.reconnect("g10/A", "N19").ok());                          // legal: N19 is not downstream of g10
  applySeeds(t, e); CHECK_MATCH(m, t);
  CHECK(m.acyclic() && m.invariants().empty());
}

static void testC17ScriptFile() {
  MockBackend m; loadFile(m, "c17.v"); m.setClockPeriod(1.0);
  ECOManager e(m); RefTimer t(m); t.fullUpdate();
  double before = t.worstSlack();
  std::ifstream f(dataDir() + "/eco/c17_edits.txt");
  CHECK(f.good());
  std::size_t applied = 0;
  EcoResult r = e.applyScript(f, &applied);
  CHECK(r.ok() && applied == 3);
  CHECK(m.clockPeriod() == 1.5);
  applySeeds(t, e); CHECK_MATCH(m, t);
  CHECK(t.worstSlack() != before);
  // error mid-script: earlier lines stay applied, checkpoint rollback reverts them
  ECOManager::Checkpoint cp = e.checkpoint();
  std::string snap = m.snapshot();
  std::istringstream bad("swap_cell g10 sky130_fd_sc_hd__nand2_4\nswap_cell nosuch sky130_fd_sc_hd__nand2_2\n");
  r = e.applyScript(bad, &applied);
  CHECK(r.status == EcoStatus::UnknownInstance && r.message.rfind("line 2", 0) == 0 && applied == 1);
  e.undoTo(cp);
  CHECK(m.snapshot() == snap);
  applySeeds(t, e); CHECK_MATCH(m, t);
}

// ------------------------------------------------------------------ boundary designs
static void testTinyDesigns() {
  {  // no instances at all
    MockBackend m;
    loadText(m, "module e ();\nendmodule\n");
    ECOManager e(m); RefTimer t(m); t.fullUpdate();
    CHECK(t.worstSlack() == RefTimer::kInf);
    CHECK(e.swapCell("x", "sky130_fd_sc_hd__inv_2").status == EcoStatus::UnknownInstance);
    CHECK(e.insertBuffer("n", "sky130_fd_sc_hd__buf_1").status == EcoStatus::UnknownNet);
    CHECK(e.setClockPeriod(2.0).ok());
    applySeeds(t, e); CHECK_MATCH(m, t);
  }
  {  // single gate, both inputs on one net, one floating-free output
    MockBackend m;
    loadText(m, "module s (a, y);\n input a;\n output y;\n sky130_fd_sc_hd__nand2_1 g (.A(a), .B(a), .Y(y));\nendmodule\n");
    m.setClockPeriod(1.0);
    ECOManager e(m); RefTimer t(m); t.fullUpdate();
    CHECK(m.sinkPinNames("a").size() == 2);
    CHECK(e.swapCell("g", "sky130_fd_sc_hd__nand2_4").ok()); applySeeds(t, e); CHECK_MATCH(m, t);
    CHECK(e.insertBuffer("a", "sky130_fd_sc_hd__buf_1", {"g/A"}).ok()); applySeeds(t, e); CHECK_MATCH(m, t);  // only one of the two pins
    CHECK(m.netOf(m.findPin("g/B")) == m.findNet("a"));
    CHECK(e.reconnect("g/B", "y").status == EcoStatus::WouldCreateCycle);                                     // own output
    e.undoAll(); applySeeds(t, e); CHECK_MATCH(m, t);
  }
  {  // floating input pin and a dangling (fanout-0) output
    MockBackend m;
    loadText(m, "module f (a, y);\n input a;\n output y;\n wire dead;\n"
                " sky130_fd_sc_hd__nand2_1 g (.A(a), .B(), .Y(y));\n sky130_fd_sc_hd__inv_1 d (.A(a), .Y(dead));\nendmodule\n");
    m.setClockPeriod(1.0);
    ECOManager e(m); RefTimer t(m); t.fullUpdate();
    CHECK(m.netOf(m.findPin("g/B")) == kNil);
    CHECK(e.swapCell("g", "sky130_fd_sc_hd__nand2_2").ok()); applySeeds(t, e); CHECK_MATCH(m, t);
    CHECK(e.insertBuffer("dead", "sky130_fd_sc_hd__buf_1").status == EcoStatus::EmptySinkList);               // net with no sinks
    CHECK(e.reconnect("g/B", "a").ok()); applySeeds(t, e); CHECK_MATCH(m, t);                                 // connect the floating pin
  }
}

static void testHighFanout() {
  MockBackend m; loadText(m, genFanoutVerilog(500)); m.setClockPeriod(1.0);
  ECOManager e(m); RefTimer t(m); t.fullUpdate();
  RefTimer original(m); original.fullUpdate();
  std::string snap = m.snapshot();
  CHECK(m.sinkPinNames("a").size() == 500);
  EcoResult all = e.insertBuffer("a", "sky130_fd_sc_hd__buf_4");                    // buffer all 500 sinks
  CHECK(all.ok()); applySeeds(t, e); CHECK_MATCH(m, t);
  CHECK(m.sinkPinNames("a").size() == 1);
  CHECK(e.removeBuffer(all.created).ok()); applySeeds(t, e); CHECK_MATCH(m, t);
  CHECK(m.sinkPinNames("a").size() == 500 && t.sameAs(original));
  std::vector<std::string> some = {"u0/A", "u17/A", "u499/A"};
  EcoResult part = e.insertBuffer("a", "sky130_fd_sc_hd__buf_1", some);
  CHECK(part.ok()); applySeeds(t, e); CHECK_MATCH(m, t);
  CHECK(m.sinkPinNames("a").size() == 498);
  e.undoAll(); applySeeds(t, e); CHECK(m.snapshot() == snap); CHECK_MATCH(m, t);
}

// ------------------------------------------------------------------ journal semantics
static void testCheckpointsAndCommit() {
  MockBackend m; loadText(m, genRandomVerilog(6, 10, 3, 3)); m.setClockPeriod(2.0);
  ECOManager e(m);
  std::mt19937 rng(42);
  std::string s0 = m.snapshot();
  for (int i = 0; i < 10; ++i) randomEdit(m, e, rng);
  auto cp1 = e.checkpoint(); std::string s1 = m.snapshot();
  for (int i = 0; i < 10; ++i) randomEdit(m, e, rng);
  auto cp2 = e.checkpoint(); std::string s2 = m.snapshot();
  for (int i = 0; i < 10; ++i) randomEdit(m, e, rng);
  CHECK(e.checkpoint() >= cp2 && cp2 >= cp1);
  e.undoTo(cp2); CHECK(m.snapshot() == s2);
  e.undoTo(cp2); CHECK(m.snapshot() == s2);                // undoing to the current point is a no-op
  e.undoTo(cp1); CHECK(m.snapshot() == s1);
  e.undoTo(cp2 + 1000); CHECK(m.snapshot() == s1);         // checkpoint beyond the journal: nothing to undo
  e.undoAll();       CHECK(m.snapshot() == s0);
  for (int i = 0; i < 10; ++i) randomEdit(m, e, rng);
  std::string s3 = m.snapshot();
  e.commit();
  CHECK(e.checkpoint() == 0);
  e.undoAll();
  CHECK(m.snapshot() == s3);                               // committed edits cannot be undone
}

// ------------------------------------------------------------------ the main contract checks
struct Sig {
  U cell, net, other;            // other = driver (input pins) ; sinks (output pins)
  std::vector<U> sinks;
  bool operator==(const Sig& o) const { return cell == o.cell && net == o.net && other == o.other && sinks == o.sinks; }
};
static std::map<U, Sig> signatures(const MockBackend& m) {
  std::map<U, Sig> r;
  for (U p : m.allAlivePins()) {
    Sig s{m.cellOf(m.instOf(p)), m.netOf(p), kNil, {}};
    if (s.net != kNil) { if (m.isOutputPin(p)) s.sinks = m.sinksOf(s.net); else s.other = m.driverOf(s.net); }
    r[p] = s;
  }
  return r;
}

// Completeness: any pin whose local timing context changed (its cell, its net, its driver, or -- for
// outputs -- its load) MUST appear in the seeds, otherwise the engine would silently miss it.
static void testSeedCompleteness() {
  int edits = 0;
  for (unsigned seed = 1; seed <= 40; ++seed) {
    MockBackend m; loadText(m, genRandomVerilog(7, 12, seed, seed % 2 ? 3 : 0)); m.setClockPeriod(2.0);
    ECOManager e(m);
    std::mt19937 rng(seed * 31 + 7);
    for (int step = 0; step < 40; ++step) {
      auto before = signatures(m);
      EcoResult r = randomEdit(m, e, rng);
      DirtySeeds s = e.takeSeeds();
      if (!r.ok()) { CHECK(s.empty()); continue; }                 // rejected edits dirty nothing
      ++edits;
      std::set<U> seeded(s.arrival.begin(), s.arrival.end());
      seeded.insert(s.requirement.begin(), s.requirement.end());
      for (auto& kv : signatures(m)) {
        auto it = before.find(kv.first);
        bool changed = it == before.end() || !(it->second == kv.second);
        if (changed) CHECK(seeded.count(kv.first));                 // every changed pin must be seeded
      }
    }
  }
  CHECK(edits > 300);                                              // the generator really exercised edits
}

// The big one: after EVERY edit (single or batched), seed-driven incremental timing equals a full
// re-time bit-for-bit, and undo restores the original timing exactly.
static void testIncrementalEqualsFull() {
  for (unsigned seed = 1; seed <= 40; ++seed) {
    MockBackend m; loadText(m, genRandomVerilog(8, 12, seed, seed % 3 == 0 ? 0 : 3)); m.setClockPeriod(1.5);
    ECOManager e(m); RefTimer t(m); t.fullUpdate();
    RefTimer original(m); original.fullUpdate();
    std::string orig = m.snapshot();
    std::mt19937 rng(seed * 7919);
    ECOManager::Checkpoint cp = 0; std::string snapAt; bool haveCp = false;
    for (int step = 0; step < 40; ++step) {
      randomEdit(m, e, rng);
      if (rng() % 3 == 0) randomEdit(m, e, rng);                   // sometimes batch two edits per update
      applySeeds(t, e);
      CHECK_MATCH(m, t);
      if (step == 15) { cp = e.checkpoint(); snapAt = m.snapshot(); haveCp = true; }
    }
    if (haveCp) { e.undoTo(cp); applySeeds(t, e); CHECK(m.snapshot() == snapAt); CHECK_MATCH(m, t); }
    e.undoAll(); applySeeds(t, e);
    CHECK(m.snapshot() == orig);
    CHECK_MATCH(m, t);
    CHECK(t.sameAs(original));                                     // identical to the timing we started with
  }
}

// Same property, on a design large enough to show how much work incremental really saves.
static void testStressLarge() {
  int levels = g_stress ? 60 : 24, width = g_stress ? 300 : 100, edits = g_stress ? 600 : 250;
  MockBackend m; loadText(m, genRandomVerilog(levels, width, 99, 6)); m.setClockPeriod(4.0);
  ECOManager e(m); RefTimer t(m); t.fullUpdate();
  RefTimer original(m); original.fullUpdate();
  std::string orig = m.snapshot();
  std::size_t P = t.livePins(), visited = 0, applied = 0;
  std::mt19937 rng(2024);
  double usEdit = 0;
  for (int i = 0; i < edits; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    EcoResult r = randomEdit(m, e, rng);
    applySeeds(t, e);
    usEdit += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    if (r.ok()) ++applied;
    visited += t.visitedArrival() + t.visitedRequired();
    if (i % 50 == 49) CHECK_MATCH(m, t);
  }
  CHECK(m.invariants().empty() && m.acyclic());
  CHECK_MATCH(m, t);
  RefTimer fresh(m);
  auto f0 = std::chrono::steady_clock::now();
  fresh.fullUpdate();
  double usFull = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - f0).count();
  e.undoAll(); applySeeds(t, e);
  CHECK(m.snapshot() == orig);
  CHECK(t.sameAs(original));
  std::cout << "           stress: " << P << " pins, " << applied << "/" << edits << " edits applied; avg "
            << visited / edits << " pin-evaluations per edit vs " << 2 * P << " for a full update ("
            << (100.0 * visited / edits) / (2.0 * P) << "%); edit+incremental " << usEdit / edits
            << " us avg vs full " << usFull << " us\n";
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--stress") g_stress = true;
  RUN(testLoadLiberty);
  RUN(testLoadC17);
  RUN(testLoadYosysStyle);
  RUN(testLoadErrors);
  RUN(testRefTimerInvariants);
  RUN(testC17EveryEdit);
  RUN(testC17Reconnect);
  RUN(testC17ScriptFile);
  RUN(testTinyDesigns);
  RUN(testHighFanout);
  RUN(testCheckpointsAndCommit);
  RUN(testSeedCompleteness);
  RUN(testIncrementalEqualsFull);
  RUN(testStressLarge);
  std::cout << "\n==== Summary ====\nTests:  " << g_tests_passed << "/" << g_tests << " passed\nChecks: "
            << (g_checks - g_fail) << "/" << g_checks << " passed\n";
  for (auto& n : g_failed_tests) std::cout << "  failed: " << n << "\n";
  return g_fail ? 1 : 0;
}
