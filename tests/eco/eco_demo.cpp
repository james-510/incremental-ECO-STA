// End-to-end demo of the ECO workflow on a parsed (or generated) netlist:
//   load lib + netlist -> full timing -> worst path -> apply ECO script -> incremental update
//   -> compare against full re-timing -> optionally undo and verify.
// Timing here comes from the simple reference model in ref_timer.hpp (NOT Liberty tables); swap in
// Dongran's TimingEngine behind the same seeds to turn this into the real demo.
//
//   eco_demo --lib data/lib/mini_sky130.lib --netlist data/netlists/c17.v
//            --eco data/eco/c17_edits.txt --period 1.0 [--undo]   (one command line)
//   eco_demo --gen 20,100,7,5 --eco my_edits.txt --dump-verilog gen.v      (levels,width,seed,ffEvery)
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include "eco/eco_manager.hpp"
#include "mock_backend.hpp"
#include "netlist_loader.hpp"
#include "design_gen.hpp"
#include "ref_timer.hpp"

using namespace eco;
using Clock = std::chrono::steady_clock;
static double usSince(Clock::time_point t0) { return std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); }

static void usage() {
  std::cerr << "usage: eco_demo (--netlist F.v --lib F.lib | --gen L,W,seed[,ffEvery]) [--lib F.lib]\n"
               "                [--eco edits.txt] [--period ns] [--undo] [--dump-verilog out.v]\n";
}
static void report(const char* tag, const MockBackend& m, const RefTimer& t) {
  MockBackend::U ep;
  double w = t.worstSlack(&ep);
  std::cout << "[" << tag << "] worst slack = " << w << " ns";
  if (ep != MockBackend::kNil) std::cout << "  endpoint = " << m.pinName(ep);
  std::cout << "\n  path:";
  for (auto p : t.criticalPath()) std::cout << " " << m.pinName(p);
  std::cout << "\n";
}

int main(int argc, char** argv) {
  std::string lib, netlist, eco, gen, dump;
  double period = 2.0;
  bool undo = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() -> std::string { if (i + 1 >= argc) { usage(); std::exit(2); } return argv[++i]; };
    if (a == "--lib") lib = val();
    else if (a == "--netlist") netlist = val();
    else if (a == "--eco") eco = val();
    else if (a == "--gen") gen = val();
    else if (a == "--dump-verilog") dump = val();
    else if (a == "--period") period = std::stod(val());
    else if (a == "--undo") undo = true;
    else { usage(); return 2; }
  }
  if (lib.empty() || (netlist.empty() && gen.empty())) { usage(); return 2; }

  try {
    MockBackend m;
    std::size_t cells = loadLiberty(lib, m);
    LoadStats st;
    if (!gen.empty()) {
      int L = 0, W = 0, ff = 0; unsigned seed = 1;
      if (std::sscanf(gen.c_str(), "%d,%d,%u,%d", &L, &W, &seed, &ff) < 2) { usage(); return 2; }
      std::string v = genRandomVerilog(L, W, seed, ff);
      if (!dump.empty()) { std::ofstream(dump) << v; std::cout << "wrote " << dump << "\n"; }
      st = loadVerilogText(v, m);
    } else st = loadVerilog(netlist, m);
    m.setClockPeriod(period);
    std::cout << "design " << st.module << ": " << st.insts << " cells, " << st.nets << " nets, " << st.inputs << " inputs, "
              << st.outputs << " outputs  (library: " << cells << " cells), clock period " << period << " ns\n";

    ECOManager e(m);
    RefTimer t(m);
    auto t0 = Clock::now();
    t.fullUpdate();
    double usFull0 = usSince(t0);
    RefTimer original(m);
    original.fullUpdate();
    std::string snap = m.snapshot();
    report("before", m, t);
    std::cout << "  full timing: " << usFull0 << " us\n";

    if (!eco.empty()) {
      std::ifstream f(eco);
      if (!f) { std::cerr << "cannot open " << eco << "\n"; return 1; }
      std::size_t applied = 0;
      EcoResult r = e.applyScript(f, &applied);
      std::cout << "[eco] applied " << applied << " edit(s)";
      if (!r.ok()) std::cout << "; stopped: " << r.message;
      std::cout << "\n";

      DirtySeeds s = e.takeSeeds();
      t0 = Clock::now();
      t.incrementalUpdate(s.arrival, s.requirement, s.structural);
      double usInc = usSince(t0);
      std::size_t P = t.livePins();
      std::cout << "[incremental] seeds: " << s.arrival.size() << " arrival + " << s.requirement.size() << " required"
                << (s.structural ? " (structural)" : "") << "; pins re-evaluated: forward " << t.visitedArrival()
                << ", backward " << t.visitedRequired() << " of " << P << " each; " << usInc << " us\n";
      report("after ", m, t);

      RefTimer full(m);
      t0 = Clock::now();
      full.fullUpdate();
      double usFull = usSince(t0);
      std::string why;
      bool ok = t.sameAs(full, &why);
      std::cout << "[full re-time] " << usFull << " us (" << usFull / (usInc > 0 ? usInc : 1) << "x the incremental time)\n";
      std::cout << "check vs full re-timing: " << (ok ? "MATCH" : "MISMATCH: " + why) << "\n";
      if (!ok) return 1;

      if (undo) {
        e.undoAll();
        DirtySeeds u = e.takeSeeds();
        t.incrementalUpdate(u.arrival, u.requirement, u.structural);
        bool same = m.snapshot() == snap && t.sameAs(original, &why);
        std::cout << "undo: netlist + timing restored: " << (same ? "OK" : "FAILED: " + why) << "\n";
        if (!same) return 1;
      }
      if (!r.ok()) return 1;
    }
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
