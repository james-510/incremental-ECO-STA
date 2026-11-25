// Plain-assert style so it builds anywhere; port to GoogleTest by swapping CHECK for EXPECT_TRUE.
#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include "eco/eco_manager.hpp"
#include "mock_backend.hpp"

using namespace eco;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #c "\n"; } } while (0)

// Per-test bookkeeping: each test function is run through runTest(), which reports
// PASS/FAIL based on whether any CHECK inside it failed.
static int g_tests = 0, g_tests_passed = 0;
static std::vector<std::string> g_failed_tests;

static void runTest(const char* name, void (*fn)()) {
    const int fail0 = g_fail, checks0 = g_checks;
    fn();
    const int checks = g_checks - checks0, failed = g_fail - fail0;
    const bool ok = (failed == 0);
    ++g_tests;
    if (ok) {
        ++g_tests_passed; 
    }
    else {
        g_failed_tests.push_back(name);
    }
    std::cout << (ok ? "[  PASS  ] " : "[  FAIL  ] ") << name
              << "  (" << (checks - failed) << "/" << checks << " checks)\n";
}

// in1 -> u1(INV) -> u2(NAND2).A ; in2 -> u2.B ; u2 -> f1.D ; f1.Q -> out1 ; clk -> f1.CK
// in1 also feeds u3(INV) -> out2   (so n_in1 has two sinks)
static void build(MockBackend& m) {
    m.defCell({"PORT_IN", "port", {}, {"P"}, false, false, true});
    m.defCell({"PORT_OUT", "port", {"P"}, {}, false, false, true});
    m.defCell({"INV_X1", "inv", {"A"}, {"ZN"}});
    m.defCell({"INV_X2", "inv", {"A"}, {"ZN"}});
    m.defCell({"NAND2_X1", "nand2", {"A", "B"}, {"ZN"}});
    m.defCell({"NAND2_X2", "nand2", {"A", "B"}, {"ZN"}});
    m.defCell({"BUF_X1", "buf", {"A"}, {"Z"}, false, true});
    m.defCell({"BUF_X2", "buf", {"A"}, {"Z"}, false, true});
    m.defCell({"DFF_X1", "dff", {"D", "CK"}, {"Q"}, true});
    for (auto n : {"n_in1", "n_in2", "n_clk", "n1", "n2", "n3", "n_q"}) m.makeNet(n);
    for (auto p : {"in1", "in2", "clk"}) m.makeInst(p, "PORT_IN");
    for (auto p : {"out1", "out2"}) m.makeInst(p, "PORT_OUT");
    m.makeInst("u1", "INV_X1"); m.makeInst("u2", "NAND2_X1"); m.makeInst("u3", "INV_X1"); m.makeInst("f1", "DFF_X1");
    const char* wires[][2] = {{"in1/P", "n_in1"}, {"u1/A", "n_in1"}, {"u3/A", "n_in1"}, {"u1/ZN", "n1"}, {"u2/A", "n1"},
                                {"in2/P", "n_in2"}, {"u2/B", "n_in2"}, {"u2/ZN", "n2"}, {"f1/D", "n2"},
                                {"clk/P", "n_clk"}, {"f1/CK", "n_clk"}, {"f1/Q", "n_q"}, {"out1/P", "n_q"},
                                {"u3/ZN", "n3"}, {"out2/P", "n3"}};
    for (auto& w : wires) m.connect(w[0], w[1]);
    m.setClockPeriod(2.0);
}

static bool has(const std::vector<PinID>& v, const MockBackend& m, const char* pin) {
    return std::find(v.begin(), v.end(), m.findPin(pin)) != v.end();
}

static void testSwap() {
    MockBackend m; build(m); ECOManager e(m);
    std::string before = m.snapshot();
    CHECK(e.swapCell("u2", "NAND2_X2").ok());
    DirtySeeds s = e.takeSeeds();
    CHECK(has(s.arrival, m, "u2/ZN"));       // cell's own arc changed
    CHECK(has(s.arrival, m, "u1/ZN"));       // driver of u2.A sees new pin cap
    CHECK(has(s.arrival, m, "in2/P"));       // driver of u2.B sees new pin cap
    CHECK(has(s.requirement, m, "u2/A"));
    CHECK(!s.structural);                    // a swap does not change graph shape
    CHECK(e.swapCell("u2", "NAND2_X2").ok() && e.takeSeeds().empty());  // no-op
    e.undoAll();
    CHECK(m.snapshot() == before);
    CHECK(!e.takeSeeds().empty());           // undo also dirties the same region
}

static void testBadInputs() {
    MockBackend m; build(m); ECOManager e(m);
    std::string before = m.snapshot();
    CHECK(e.swapCell("u2", "INV_X2").status == EcoStatus::IncompatibleCell);
    CHECK(e.swapCell("nope", "INV_X2").status == EcoStatus::UnknownInstance);
    CHECK(e.swapCell("u1", "nope").status == EcoStatus::UnknownCell);
    CHECK(e.insertBuffer("n2", "INV_X1").status == EcoStatus::NotABuffer);
    CHECK(e.insertBuffer("n2", "BUF_X1", {"u1/A"}).status == EcoStatus::SinkNotOnNet);
    CHECK(e.insertBuffer("n3x", "BUF_X1").status == EcoStatus::UnknownNet);
    CHECK(e.removeBuffer("u1").status == EcoStatus::NotABuffer);
    CHECK(e.reconnect("u1/ZN", "n2").status == EcoStatus::PinNotASink);
    CHECK(e.setClockPeriod(0).status == EcoStatus::BadPeriod);
    CHECK(m.snapshot() == before);           // failures change nothing
    CHECK(e.checkpoint() == 0);
}

static void testInsertRemoveBuffer() {
    MockBackend m; build(m); ECOManager e(m);
    std::string before = m.snapshot();
    EcoResult r = e.insertBuffer("n_in1", "BUF_X1", {"u3/A"});   // buffer only one of two sinks
    CHECK(r.ok() && !r.created.empty());
    DirtySeeds s = e.takeSeeds();
    CHECK(s.structural);
    CHECK(has(s.arrival, m, "u3/A"));
    CHECK(has(s.arrival, m, "in1/P"));       // driver's load changed
    CHECK(m.netOf(m.findPin("u1/A")) == m.findNet("n_in1"));      // untouched sink stays
    CHECK(m.netOf(m.findPin("u3/A")) != m.findNet("n_in1"));      // moved sink
    CHECK(m.invariants().empty() && m.acyclic());
    CHECK(e.removeBuffer(r.created).ok());
    CHECK(m.snapshot() == before);           // insert + remove round-trips exactly
    e.undoAll();
    CHECK(m.snapshot() == before);
    // buffer all sinks of n2, then undo
    CHECK(e.insertBuffer("n2", "BUF_X2").ok());
    e.undoAll();
    CHECK(m.snapshot() == before);
}

static void testRemoveProtectedPortNet() {
    MockBackend m; build(m); ECOManager e(m);
    EcoResult r = e.insertBuffer("n3", "BUF_X1");   // n3 feeds port out2; buffer all sinks moves out2 behind the buffer
    CHECK(r.ok());
    // The buffer's OUTPUT net now feeds a port, so removing it must be refused.
    CHECK(e.removeBuffer(r.created).status == EcoStatus::PortNetProtected);
}

static void testReconnectCycles() {
    MockBackend m; build(m); ECOManager e(m);
    std::string before = m.snapshot();
    CHECK(e.reconnect("u1/A", "n1").status == EcoStatus::WouldCreateCycle);   // own output
    CHECK(e.reconnect("u1/A", "n2").status == EcoStatus::WouldCreateCycle);   // u1->u2->u1
    CHECK(m.snapshot() == before);
    CHECK(e.reconnect("f1/D", "n_in1").ok());                                 // flop breaks loops
    CHECK(e.reconnect("u2/B", "n_in1").ok());
    CHECK(m.acyclic() && m.invariants().empty());
    CHECK(e.takeSeeds().structural);
    e.undoAll();
    CHECK(m.snapshot() == before);
}

static void testClockPeriod() {
    MockBackend m; build(m); ECOManager e(m);
    CHECK(e.setClockPeriod(3.5).ok());
    DirtySeeds s = e.takeSeeds();
    CHECK(s.arrival.empty());                // period only moves required times
    CHECK(has(s.requirement, m, "f1/D") && has(s.requirement, m, "out1/P"));
    CHECK(m.clockPeriod() == 3.5);
    e.undoAll();
    CHECK(m.clockPeriod() == 2.0);
}

static void testScript() {
    MockBackend m; build(m); ECOManager e(m);
    std::string before = m.snapshot();
    std::istringstream in("# demo\nswap_cell u2 NAND2_X2\n\ninsert_buffer n2 BUF_X1\nset_clock_period 3.0\nbogus x\n");
    std::size_t applied = 0;
    EcoResult r = e.applyScript(in, &applied);
    CHECK(r.status == EcoStatus::BadCommand && r.message.rfind("line 6", 0) == 0);
    CHECK(applied == 3);
    e.undoAll();
    CHECK(m.snapshot() == before);
}

// Property test: random edit sequences keep invariants, failed edits change nothing,
// and undoAll() always restores the original netlist exactly.
static void testRandomized() {
    for (unsigned seed = 1; seed <= 300; ++seed) {
        MockBackend m; build(m); ECOManager e(m);
        std::string original = m.snapshot();
        std::mt19937 rng(seed);
        auto pick = [&](const std::vector<std::string>& v) -> const std::string& { return v[rng() % v.size()]; };
        for (int step = 0; step < 40; ++step) {
            std::string pre = m.snapshot();
            EcoResult r;
            switch (rng() % 5) {
                case 0: 
                    r = e.swapCell(pick(m.aliveInstNames()), pick(m.cellNames())); 
                    break;
                case 1: {
                    std::string net = pick(m.aliveNetNames());
                    auto sinks = m.sinkPinNames(net);
                    std::vector<std::string> subset;
                    for (auto& s : sinks) if (rng() & 1) subset.push_back(s);
                    r = e.insertBuffer(net, rng() & 1 ? "BUF_X1" : "BUF_X2", subset);
                    break;
                }
                case 2: { 
                    auto b = m.bufferInstNames(); r = b.empty() ? EcoResult{} : e.removeBuffer(pick(b)); 
                    break; 
                }
                case 3: {
                    auto insts = m.aliveInstNames();
                    auto pins = m.inputPinNames(pick(insts));
                    r = pins.empty() ? EcoResult{} : e.reconnect(pick(pins), pick(m.aliveNetNames()));
                    break;
                }
                default: 
                    r = e.setClockPeriod(0.5 + (rng() % 40) / 10.0); 
                    break;
            }
            if (!r.ok()) CHECK(m.snapshot() == pre);
            CHECK(m.invariants().empty());
            CHECK(m.acyclic());
            if (step % 10 == 9) e.takeSeeds();
        }
        e.undoAll();
        CHECK(m.snapshot() == original);
    }
}

#define RUN(t) runTest(#t, t)

int main() {
    RUN(testSwap);
    RUN(testBadInputs);
    RUN(testInsertRemoveBuffer);
    RUN(testRemoveProtectedPortNet);
    RUN(testReconnectCycles);
    RUN(testClockPeriod);
    RUN(testScript);
    RUN(testRandomized);

    std::cout << "\n==== Summary ====\n"
              << "Tests:  " << g_tests_passed << "/" << g_tests << " passed\n"
              << "Checks: " << (g_checks - g_fail) << "/" << g_checks << " passed\n";
    for (auto& n : g_failed_tests) {
        std::cout << "  failed: " << n << "\n";
    }

    return g_fail ? 1 : 0;
}