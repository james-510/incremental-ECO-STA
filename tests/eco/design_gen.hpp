// Deterministic synthetic netlists (flat Verilog text using SKY130 cell names), so tests and the
// demo don't need external files. Feed the same text to OpenSTA for reference comparisons
// (eco_demo --dump-verilog).
#pragma once
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace eco {

// `levels` x `width` combinational gates with reconvergent fanout, biased toward a few high-fanout
// signals; if ffEvery>0, every ffEvery-th level is registered by dfxtp_1 flops on `clk`.
inline std::string genRandomVerilog(int levels, int width, unsigned seed, int ffEvery = 0) {
  struct G { const char* cell; std::vector<const char*> in; const char* out; };
  static const std::vector<G> types = {
      {"inv_1", {"A"}, "Y"},          {"nand2_1", {"A", "B"}, "Y"}, {"nor2_1", {"A", "B"}, "Y"},
      {"and2_1", {"A", "B"}, "X"},    {"or2_1", {"A", "B"}, "X"},   {"xor2_1", {"A", "B"}, "X"},
      {"nand3_1", {"A", "B", "C"}, "Y"}, {"mux2_1", {"A0", "A1", "S"}, "X"}};
  std::mt19937 rng(seed);
  std::vector<std::vector<std::string>> sig;
  std::vector<std::string> ins, outs, wires, header;
  std::ostringstream body;
  std::vector<std::string> l0;
  for (int i = 0; i < width; ++i) { l0.push_back("i" + std::to_string(i)); ins.push_back(l0.back()); }
  sig.push_back(l0);
  auto pick = [&]() {
    std::size_t lv = (rng() % 4) ? sig.size() - 1 : rng() % sig.size();
    const auto& v = sig[lv];
    double u = (rng() % 10000) / 10000.0;
    return v[static_cast<std::size_t>(u * u * v.size())];
  };
  int g = 0;
  for (int L = 1; L <= levels; ++L) {
    std::vector<std::string> cur;
    for (int w = 0; w < width; ++w) {
      const G& T = types[rng() % types.size()];
      std::string o = (L == levels) ? "o" + std::to_string(w) : "n" + std::to_string(L) + "_" + std::to_string(w);
      body << "  sky130_fd_sc_hd__" << T.cell << " g" << g++ << " (";
      for (std::size_t k = 0; k < T.in.size(); ++k) { std::string s = pick(); body << "." << T.in[k] << "(" << s << "), "; }
      body << "." << T.out << "(" << o << "));\n";
      cur.push_back(o);
      (L == levels ? outs : wires).push_back(o);
    }
    if (L < levels && ffEvery > 0 && L % ffEvery == 0) {
      std::vector<std::string> regs;
      for (int w = 0; w < width; ++w) {
        std::string r = "r" + std::to_string(L) + "_" + std::to_string(w);
        body << "  sky130_fd_sc_hd__dfxtp_1 f" << L << "_" << w << " (.CLK(clk), .D(" << cur[w] << "), .Q(" << r << "));\n";
        wires.push_back(r);
        regs.push_back(r);
      }
      sig.push_back(regs);
    } else sig.push_back(cur);
  }
  std::ostringstream v;
  v << "module gen (clk";
  for (auto& s : ins) v << ", " << s;
  for (auto& s : outs) v << ", " << s;
  v << ");\n  input clk;\n";
  for (auto& s : ins) v << "  input " << s << ";\n";
  for (auto& s : outs) v << "  output " << s << ";\n";
  for (auto& s : wires) v << "  wire " << s << ";\n";
  v << body.str() << "endmodule\n";
  return v.str();
}

// One input driving `n` inverters (a single net with fanout n).
inline std::string genFanoutVerilog(int n) {
  std::ostringstream v;
  v << "module fo (a";
  for (int i = 0; i < n; ++i) v << ", y" << i;
  v << ");\n  input a;\n";
  for (int i = 0; i < n; ++i) v << "  output y" << i << ";\n";
  for (int i = 0; i < n; ++i) v << "  sky130_fd_sc_hd__inv_1 u" << i << " (.A(a), .Y(y" << i << "));\n";
  v << "endmodule\n";
  return v.str();
}

}  // namespace eco
