// Test-side loaders: a minimal Liberty scanner and a flat structural Verilog reader that fill a
// MockBackend, so ECO tests can run on real library cells and real (Yosys-style) netlists.
// Replace with actual netlist parsers once they exist; the ECO code never sees this file.
//
// Liberty: reads cell names, pin directions, and whether a cell has an ff/latch group.
//          Timing tables are skipped. Works on both the mini lib and the full SKY130 lib.
// Verilog: flat, non-ANSI modules; scalar and bus declarations; named port connections;
//          escaped identifiers; (* attributes *); 1'b0/1'b1 constants. No assign/params/concat.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "mock_backend.hpp"

namespace eco {

struct LoadStats {
  std::string module;
  std::size_t insts = 0, nets = 0, inputs = 0, outputs = 0, constants = 0;
};

namespace detail {

inline std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
inline std::string trimStrip(std::string s) {
  auto sp = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && sp(s.front())) s.erase(s.begin());
  while (!s.empty() && sp(s.back())) s.pop_back();
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
  return s;
}

// ------------------------------------------------------------------ Liberty
class LibParser {
 public:
  LibParser(const std::string& text, MockBackend& m) : s_(text), m_(m) {}
  std::size_t run() {
    for (;;) {
      Stmt st = readStmt();
      if (st.kind == Stmt::Eof) break;
      if (st.kind == Stmt::Group) { if (st.name == "library") libraryBody(); else skipBody(); }
    }
    return cells_;
  }

 private:
  struct Stmt { enum Kind { Attr, Group, Other, End, Eof } kind; std::string name, arg; };
  const std::string& s_;
  MockBackend& m_;
  std::size_t i_ = 0, cells_ = 0;

  void ws() {
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (std::isspace(static_cast<unsigned char>(c))) { ++i_; }
      else if (c == '\\' && i_ + 1 < s_.size() && (s_[i_ + 1] == '\n' || s_[i_ + 1] == '\r')) { i_ += 2; }
      else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '*') { auto e = s_.find("*/", i_ + 2); i_ = (e == std::string::npos) ? s_.size() : e + 2; }
      else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') { while (i_ < s_.size() && s_[i_] != '\n') ++i_; }
      else break;
    }
  }
  std::string word() {
    if (s_[i_] == '"') return quoted();
    std::size_t j = i_;
    while (j < s_.size() && !std::isspace(static_cast<unsigned char>(s_[j])) && !std::strchr("(){}:;,\"", s_[j])) ++j;
    std::string w = s_.substr(i_, j - i_);
    i_ = j;
    return w;
  }
  std::string quoted() {  // at opening quote; returns text without quotes
    std::size_t j = i_ + 1;
    while (j < s_.size() && s_[j] != '"') { if (s_[j] == '\\') ++j; ++j; }
    std::string w = s_.substr(i_ + 1, j - i_ - 1);
    i_ = (j < s_.size()) ? j + 1 : j;
    return w;
  }
  std::string value() {  // after ':' up to ';'
    ws();
    std::string v;
    while (i_ < s_.size() && s_[i_] != ';' && s_[i_] != '}') {
      if (s_[i_] == '"') { v += '"' + quoted() + '"'; }
      else v += s_[i_++];
    }
    if (i_ < s_.size() && s_[i_] == ';') ++i_;
    return trimStrip(v);
  }
  std::string args() {  // at '(' ; returns inside, balanced
    ++i_;
    std::string a;
    int depth = 1;
    while (i_ < s_.size() && depth > 0) {
      char c = s_[i_];
      if (c == '"') { a += '"' + quoted() + '"'; continue; }
      if (c == '(') ++depth;
      else if (c == ')') { if (--depth == 0) { ++i_; break; } }
      a += c;
      ++i_;
    }
    return trimStrip(a);
  }
  void skipBody() {  // after '{' ; consume through matching '}'
    int depth = 1;
    while (i_ < s_.size() && depth > 0) {
      char c = s_[i_];
      if (c == '"') { quoted(); continue; }
      if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '*') { auto e = s_.find("*/", i_ + 2); i_ = (e == std::string::npos) ? s_.size() : e + 2; continue; }
      if (c == '{') ++depth;
      else if (c == '}') --depth;
      ++i_;
    }
  }
  Stmt readStmt() {
    ws();
    if (i_ >= s_.size()) return {Stmt::Eof, "", ""};
    if (s_[i_] == '}') { ++i_; return {Stmt::End, "", ""}; }
    std::string name = word();
    if (name.empty()) { ++i_; return {Stmt::Other, "", ""}; }
    ws();
    if (i_ < s_.size() && s_[i_] == ':') { ++i_; return {Stmt::Attr, name, value()}; }
    if (i_ < s_.size() && s_[i_] == '(') {
      std::string a = args();
      ws();
      if (i_ < s_.size() && s_[i_] == '{') { ++i_; return {Stmt::Group, name, a}; }
      if (i_ < s_.size() && s_[i_] == ';') ++i_;
      return {Stmt::Other, name, a};
    }
    while (i_ < s_.size() && s_[i_] != ';' && s_[i_] != '}') ++i_;   // unknown junk: resync
    if (i_ < s_.size() && s_[i_] == ';') ++i_;
    return {Stmt::Other, name, ""};
  }
  void libraryBody() {
    for (;;) {
      Stmt st = readStmt();
      if (st.kind == Stmt::End || st.kind == Stmt::Eof) return;
      if (st.kind == Stmt::Group) { if (st.name == "cell") cellBody(trimStrip(st.arg)); else skipBody(); }
    }
  }
  std::string pinBody() {
    std::string dir;
    for (;;) {
      Stmt st = readStmt();
      if (st.kind == Stmt::End || st.kind == Stmt::Eof) return dir;
      if (st.kind == Stmt::Attr && st.name == "direction") dir = st.arg;
      else if (st.kind == Stmt::Group) skipBody();
    }
  }
  void cellBody(const std::string& name) {
    std::vector<std::string> in, out;
    bool seq = false;
    for (;;) {
      Stmt st = readStmt();
      if (st.kind == Stmt::End || st.kind == Stmt::Eof) break;
      if (st.kind != Stmt::Group) continue;
      if (st.name == "pin") {
        std::string pn = trimStrip(st.arg), dir = pinBody();
        if (dir == "input") in.push_back(pn);
        else if (dir == "output") out.push_back(pn);
      } else if (st.name == "ff" || st.name == "latch") { seq = true; skipBody(); }
      else skipBody();
    }
    std::sort(in.begin(), in.end());
    std::sort(out.begin(), out.end());
    MockCellDef d;
    d.name = name;
    d.in = in; d.out = out; d.seq = seq;
    // footprint = name without the trailing drive-strength suffix (_1, _2, ...): cells sharing it can be swapped
    std::size_t us = name.rfind('_');
    bool digits = us != std::string::npos && us + 1 < name.size() &&
                  name.find_first_not_of("0123456789", us + 1) == std::string::npos;
    d.footprint = digits ? name.substr(0, us) : name;
    std::size_t dd = d.footprint.rfind("__");
    std::string base = dd == std::string::npos ? d.footprint : d.footprint.substr(dd + 2);
    d.buf = (base == "buf" || base == "clkbuf") && in.size() == 1 && out.size() == 1;
    m_.defCell(d);
    ++cells_;
  }
};

// ------------------------------------------------------------------ Verilog
inline bool idStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
inline bool idChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

inline std::vector<std::string> vtokens(const std::string& s) {
  std::vector<std::string> t;
  std::size_t i = 0, n = s.size();
  while (i < n) {
    char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
    if (c == '/' && i + 1 < n && s[i + 1] == '/') { while (i < n && s[i] != '\n') ++i; continue; }
    if (c == '/' && i + 1 < n && s[i + 1] == '*') {
      auto e = s.find("*/", i + 2);
      if (e == std::string::npos) throw std::runtime_error("unterminated /* comment");
      i = e + 2; continue;
    }
    if (c == '(' && i + 1 < n && s[i + 1] == '*') {
      auto e = s.find("*)", i + 2);
      if (e == std::string::npos) throw std::runtime_error("unterminated (* attribute");
      i = e + 2; continue;
    }
    if (c == '\\') {  // escaped identifier: up to whitespace
      std::size_t j = i + 1;
      while (j < n && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
      t.push_back(s.substr(i + 1, j - i - 1));
      i = j; continue;
    }
    if (idStart(c)) { std::size_t j = i; while (j < n && idChar(s[j])) ++j; t.push_back(s.substr(i, j - i)); i = j; continue; }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::size_t j = i;
      while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || s[j] == '\'')) ++j;
      t.push_back(s.substr(i, j - i)); i = j; continue;
    }
    t.push_back(std::string(1, c));
    ++i;
  }
  return t;
}

struct VInst { std::string type, name; std::vector<std::pair<std::string, std::string>> conns; };
struct VDesign { std::string module; std::vector<std::string> inputs, outputs, wires; std::vector<VInst> insts; };

class VParser {
 public:
  explicit VParser(std::vector<std::string> t) : t_(std::move(t)) {}
  VDesign parse() {
    VDesign d;
    expect("module");
    d.module = next();
    if (peek() == "(") {
      next();
      while (peek() != ")") {
        std::string w = next();
        if (w == "input" || w == "output" || w == "inout") fail("ANSI-style port declarations in the module header are not supported");
        if (peek() == ",") next();
      }
      expect(")");
    }
    expect(";");
    for (;;) {
      if (p_ >= t_.size()) fail("missing endmodule");
      std::string w = next();
      if (w == "endmodule") break;
      if (w == "input" || w == "output" || w == "inout" || w == "wire") { decl(w, d); continue; }
      if (w == "assign" || w == "parameter" || w == "localparam" || w == "always" || w == "initial" || w == "reg")
        fail("unsupported construct '" + w + "' (flat structural netlists only)");
      d.insts.push_back(instance(w));
    }
    return d;
  }

 private:
  std::vector<std::string> t_;
  std::size_t p_ = 0;
  [[noreturn]] void fail(const std::string& m) const { throw std::runtime_error("verilog: " + m + " (token " + std::to_string(p_) + ")"); }
  const std::string& peek() const { static const std::string eof = "<eof>"; return p_ < t_.size() ? t_[p_] : eof; }
  std::string next() { if (p_ >= t_.size()) fail("unexpected end of file"); return t_[p_++]; }
  void expect(const std::string& s) { std::string g = next(); if (g != s) fail("expected '" + s + "' but found '" + g + "'"); }

  void decl(const std::string& kind, VDesign& d) {
    if (peek() == "wire" || peek() == "reg") next();
    bool range = false;
    int hi = 0, lo = 0;
    if (peek() == "[") { next(); hi = std::stoi(next()); expect(":"); lo = std::stoi(next()); expect("]"); range = true; }
    for (;;) {
      std::string nm = next();
      std::vector<std::string> names;
      if (!range) names.push_back(nm);
      else for (int k = hi; hi >= lo ? k >= lo : k <= lo; hi >= lo ? --k : ++k) names.push_back(nm + "[" + std::to_string(k) + "]");
      for (auto& x : names) (kind == "input" ? d.inputs : kind == "wire" ? d.wires : d.outputs).push_back(x);
      std::string sep = next();
      if (sep == ";") break;
      if (sep != ",") fail("expected ',' or ';' in declaration, found '" + sep + "'");
    }
  }
  VInst instance(const std::string& type) {
    VInst in;
    in.type = type;
    in.name = next();
    expect("(");
    while (peek() != ")") {
      expect(".");
      std::string pin = next();
      expect("(");
      std::string net;
      if (peek() != ")") {
        std::string tk = next();
        if (tk == "{") fail("concatenations are not supported");
        if (tk.find('\'') != std::string::npos) {
          if (tk == "1'b0" || tk == "1'h0") net = "\x01" "0";
          else if (tk == "1'b1" || tk == "1'h1") net = "\x01" "1";
          else fail("unsupported constant " + tk);
        } else {
          net = tk;
          if (peek() == "[") {
            next(); std::string idx = next();
            if (peek() != "]") fail("part selects are not supported");
            next();
            net += "[" + idx + "]";
          }
        }
      }
      expect(")");
      in.conns.push_back({pin, net});
      if (peek() == ",") next();
    }
    expect(")");
    expect(";");
    return in;
  }
};

}  // namespace detail

// Returns number of cells defined.
inline std::size_t loadLibertyText(const std::string& text, MockBackend& m) { return detail::LibParser(text, m).run(); }
inline std::size_t loadLiberty(const std::string& path, MockBackend& m) { return loadLibertyText(detail::readFile(path), m); }

inline LoadStats loadVerilogText(const std::string& text, MockBackend& m) {
  using U = MockBackend::U;
  const U nil = MockBackend::kNil;
  detail::VDesign d = detail::VParser(detail::vtokens(text)).parse();
  LoadStats st;
  st.module = d.module;
  if (m.findCell("PORT_IN") == nil) m.defCell({"PORT_IN", "port", {}, {"P"}, false, false, true});
  if (m.findCell("PORT_OUT") == nil) m.defCell({"PORT_OUT", "port", {"P"}, {}, false, false, true});

  auto net = [&](const std::string& nm) {
    U n = m.findNet(nm);
    if (n == nil) { n = m.makeNet(nm); ++st.nets; }
    return n;
  };
  auto inst = [&](const std::string& nm, const std::string& cell) {
    if (m.findInst(nm) != nil) throw std::runtime_error("duplicate instance " + nm);
    return m.makeInst(nm, cell);
  };
  for (auto& w : d.wires) net(w);
  for (auto& p : d.inputs) {
    U n = net(p);
    inst("__in_" + p, "PORT_IN");
    m.rewire(m.findPin("__in_" + p + "/P"), n);
    ++st.inputs;
  }
  for (auto& p : d.outputs) {
    U n = net(p);
    inst("__out_" + p, "PORT_OUT");
    m.rewire(m.findPin("__out_" + p + "/P"), n);
    ++st.outputs;
  }
  for (auto& g : d.insts) {
    if (m.findCell(g.type) == nil) throw std::runtime_error("unknown cell type " + g.type + " (instance " + g.name + ")");
    inst(g.name, g.type);
    ++st.insts;
    for (auto& c : g.conns) {
      U pin = m.findPin(g.name + "/" + c.first);
      if (pin == nil) throw std::runtime_error("cell " + g.type + " has no pin " + c.first + " (instance " + g.name + ")");
      if (c.second.empty()) continue;  // unconnected
      U n;
      if (c.second[0] == '\x01') {     // constant: shared tie net driven by a PORT_IN pseudo-instance
        std::string tn = std::string("__const") + c.second[1];
        if (m.findNet(tn) == nil) {
          n = net(tn);
          inst("__tie" + std::string(1, c.second[1]), "PORT_IN");
          m.rewire(m.findPin("__tie" + std::string(1, c.second[1]) + "/P"), n);
          ++st.constants;
        } else n = m.findNet(tn);
      } else n = net(c.second);
      m.rewire(pin, n);
    }
  }
  std::string bad = m.invariants();
  if (!bad.empty()) throw std::runtime_error("netlist invalid: " + bad);
  return st;
}
inline LoadStats loadVerilog(const std::string& path, MockBackend& m) { return loadVerilogText(detail::readFile(path), m); }

}  // namespace eco
