// Reference timer over MockBackend: a deliberately simple, deterministic timing model with
//   * FULL update (levelize, forward arrivals, backward required times)
//   * SEED-DRIVEN INCREMENTAL update (worklist + change-frontier pruning), the algorithm Dongran's
//     TimingEngine will implement, run on exactly the DirtySeeds that ECOManager produces.
// If incremental != full after an ECO, the ECO engine's seeds (or the contract below) are wrong.
//
// Seed contract (what DirtySeeds must guarantee):
//   arrival seeds  : pins whose arrival formula inputs changed -> recompute; propagate fanout only if value changed
//   required seeds : same for required times
// Model (ns): arrival(input pin) = arrival(net driver) (zero wire delay)
//   comb out : max(input arrivals) + 0.04 + 0.015*nIn + 0.9*load/strength
//   flop Q   : 0.25 + 1.0*load/strength          port-in : 0.1*load
//   load     : sum of sink pin caps (0.002*strength, output ports 0.005)
//   required : output port = period, flop D = period - 0.05, comb in = req(out) - delay
// Exactly the same arithmetic runs in full and incremental updates, so results must be bit-identical.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>
#include "mock_backend.hpp"

namespace eco {

class RefTimer {
  public:
    using U = std::uint32_t;
    static constexpr double kInf = 1e30;

    explicit RefTimer(const MockBackend& m) : m_(m) {}

    void fullUpdate() {
      relevel();
      arr_.assign(m_.numPins(), nan());
      req_.assign(m_.numPins(), nan());
      visA_ = visR_ = 0;
      for (U i : alive_) if (portIn(i) || seq(i)) for (U o : m_.outputPinsOf(i)) setA(o);
      for (U i : topo_) if (comb(i)) { for (U p : m_.inputPinsOf(i)) setA(p); for (U o : m_.outputPinsOf(i)) setA(o); }
      for (U i : alive_) if (portOut(i) || seq(i)) for (U p : m_.inputPinsOf(i)) setA(p);

      for (U i : alive_) if (portOut(i) || seq(i)) for (U p : m_.inputPinsOf(i)) setR(p);
      for (auto it = topo_.rbegin(); it != topo_.rend(); ++it)
        if (comb(*it)) { for (U o : m_.outputPinsOf(*it)) setR(o); for (U p : m_.inputPinsOf(*it)) setR(p); }
      for (U i : alive_) if (portIn(i) || seq(i)) for (U o : m_.outputPinsOf(i)) setR(o);
    }

    // 
    void incrementalUpdate(const std::vector<U>& arrSeeds, const std::vector<U>& reqSeeds, bool structural) {
      bool grew = arr_.size() < m_.numPins();
      arr_.resize(m_.numPins(), nan());
      req_.resize(m_.numPins(), nan());
      if (structural || grew || lvl_.size() != m_.numInsts()) relevel();
      visA_ = visR_ = 0;
      using Item = std::pair<long, U>;
      {  // forward: low level first
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        std::vector<char> inq(m_.numPins(), 0);
        auto push = [&](U p) { if (!inq[p]) { inq[p] = 1; pq.push({key(p), p}); } };
        for (U p : arrSeeds) if (m_.pinAlive(p)) push(p);
        while (!pq.empty()) {
          U p = pq.top().second; pq.pop(); inq[p] = 0;
          double v = computeArr(p); ++visA_;
          if (v == arr_[p]) continue;  // unchanged -> prune (NaN never equals, so new pins always propagate)
          arr_[p] = v;
          if (m_.isOutputPin(p)) { U n = m_.netOf(p); if (n != nil) for (U s : m_.sinksOf(n)) push(s); }
          else if (comb(m_.instOf(p))) for (U o : m_.outputPinsOf(m_.instOf(p))) push(o);
        }
      }
      {  // backward: high level first
        std::priority_queue<Item> pq;
        std::vector<char> inq(m_.numPins(), 0);
        auto push = [&](U p) { if (!inq[p]) { inq[p] = 1; pq.push({key(p), p}); } };
        for (U p : reqSeeds) if (m_.pinAlive(p)) push(p);
        while (!pq.empty()) {
          U p = pq.top().second; pq.pop(); inq[p] = 0;
          double v = computeReq(p); ++visR_;
          if (v == req_[p]) continue;
          req_[p] = v;
          if (m_.isOutputPin(p)) { if (comb(m_.instOf(p))) for (U in : m_.inputPinsOf(m_.instOf(p))) push(in); }
          else { U n = m_.netOf(p); if (n != nil) { U d = m_.driverOf(n); if (d != nil) push(d); } }
        }
      }
    }

    double arrival(U p) const { return arr_[p]; }
    double requirement(U p) const { return req_[p]; }
    double slack(U p) const { return req_[p] >= kInf ? kInf : req_[p] - arr_[p]; }
    double worstSlack(U* ep = nullptr) const {
      double w = kInf;
      U best = nil;
      for (U p : m_.endpointPins()) { double s = slack(p); if (s < w) { w = s; best = p; } }
      if (ep) *ep = best;
      return w;
    }
    // worst endpoint back to its launching source, as pin ids in forward order
    std::vector<U> criticalPath() const {
      U ep;
      worstSlack(&ep);
      std::vector<U> path;
      if (ep == nil) return path;
      U cur = ep;
      for (;;) {
        path.push_back(cur);
        if (!m_.isOutputPin(cur)) {
          U n = m_.netOf(cur);
          U d = n == nil ? nil : m_.driverOf(n);
          if (d == nil) break;
          cur = d;
        } else {
          U inst = m_.instOf(cur);
          if (!comb(inst)) break;
          U bestIn = nil;
          for (U p : m_.inputPinsOf(inst)) if (bestIn == nil || arr_[p] > arr_[bestIn]) bestIn = p;
          if (bestIn == nil) break;
          cur = bestIn;
        }
      }
      std::reverse(path.begin(), path.end());
      return path;
    }
    std::size_t visitedArrival() const { return visA_; }
    std::size_t visitedRequired() const { return visR_; }
    std::size_t livePins() const { return m_.allAlivePins().size(); }

    // exact comparison over all live pins
    bool sameAs(const RefTimer& o, std::string* why = nullptr) const {
      for (U p : m_.allAlivePins()) {
        bool bad = p >= arr_.size() || p >= o.arr_.size() || !(arr_[p] == o.arr_[p]) || !(req_[p] == o.req_[p]);
        if (bad) {
          if (why) {
            *why = "pin " + m_.pinName(p);
            if (p < arr_.size() && p < o.arr_.size())
              *why += ": arrival " + std::to_string(arr_[p]) + " vs " + std::to_string(o.arr_[p]) +
                      ", required " + std::to_string(req_[p]) + " vs " + std::to_string(o.req_[p]);
          }
          return false;
        }
      }
      return true;
    }

  private:
    static constexpr U nil = MockBackend::kNil;
    const MockBackend& m_;
    std::vector<double> arr_, req_;
    std::vector<int> lvl_;
    std::vector<U> topo_, alive_;
    std::size_t visA_ = 0, visR_ = 0;

    static double nan() { return std::numeric_limits<double>::quiet_NaN(); }
    const MockCellDef& def(U inst) const { return m_.cellDef(m_.cellOf(inst)); }
    bool portIn(U i) const { return def(i).port && !def(i).out.empty(); }
    bool portOut(U i) const { return def(i).port && def(i).out.empty(); }
    bool seq(U i) const { return def(i).seq; }
    bool comb(U i) const { return !def(i).port && !def(i).seq; }

    static int strength(const std::string& n) {
      int i = static_cast<int>(n.size());
      while (i > 0 && std::isdigit(static_cast<unsigned char>(n[i - 1]))) --i;
      if (i == static_cast<int>(n.size()) || i == 0) return 1;
      char c = n[i - 1];
      if (c != '_' && c != 'X' && c != 'x') return 1;
      return std::max(1, std::stoi(n.substr(i)));
    }
    double capOf(U inPin) const {
      const auto& d = def(m_.instOf(inPin));
      return d.port ? 0.005 : 0.002 * strength(d.name);
    }
    double load(U outPin) const {
      U n = m_.netOf(outPin);
      double l = 0.0;
      if (n != nil) for (U s : m_.sinksOf(n)) l += capOf(s);
      return l;
    }
    double combDelay(U inst, U outPin) const {
      const auto& d = def(inst);
      return 0.04 + 0.015 * static_cast<double>(d.in.size()) + 0.9 * load(outPin) / strength(d.name);
    }
    double computeArr(U p) const {
      U inst = m_.instOf(p);
      if (!m_.isOutputPin(p)) {
        U n = m_.netOf(p);
        U d = n == nil ? nil : m_.driverOf(n);
        return d == nil ? 0.0 : arr_[d];
      }
      if (portIn(inst)) return 0.1 * load(p);
      if (seq(inst)) return 0.25 + 1.0 * load(p) / strength(def(inst).name);
      double mx = 0.0;
      for (U in : m_.inputPinsOf(inst)) mx = std::max(mx, arr_[in]);
      return mx + combDelay(inst, p);
    }
    double computeReq(U p) const {
      U inst = m_.instOf(p);
      if (m_.isOutputPin(p)) {
        U n = m_.netOf(p);
        double r = kInf;
        if (n != nil) for (U s : m_.sinksOf(n)) r = std::min(r, req_[s]);
        return r;
      }
      if (portOut(inst)) return m_.clockPeriod();
      if (seq(inst)) return m_.pinBaseName(p) == "D" ? m_.clockPeriod() - 0.05 : kInf;
      double r = kInf;
      for (U o : m_.outputPinsOf(inst)) {
        double ro = req_[o];
        if (ro >= kInf) continue;
        r = std::min(r, ro - combDelay(inst, o));
      }
      return r;
    }
    void setA(U p) { arr_[p] = computeArr(p); ++visA_; }
    void setR(U p) { req_[p] = computeReq(p); ++visR_; }
    long key(U p) const {
      U inst = m_.instOf(p);
      if (!m_.isOutputPin(p) && (def(inst).port || def(inst).seq)) return 1L << 40;  // endpoint inputs last/first
      return 2L * lvl_[inst] + (m_.isOutputPin(p) ? 1 : 0);
    }
    void relevel() {
      std::size_t N = m_.numInsts();
      lvl_.assign(N, 0);
      topo_.clear();
      alive_ = m_.aliveInsts();
      std::vector<std::vector<U>> adj(N);
      std::vector<int> indeg(N, 0);
      for (U n = 0; n < m_.numNets(); ++n) {
        if (!m_.netAlive(n)) continue;
        U d = m_.driverOf(n);
        if (d == nil) continue;
        for (U s : m_.sinksOf(n)) {
          U x = m_.instOf(s);
          if (seq(x)) continue;
          adj[m_.instOf(d)].push_back(x);
          ++indeg[x];
        }
      }
      std::vector<U> q;
      for (U i : alive_) if (!indeg[i]) q.push_back(i);
      std::size_t head = 0;
      while (head < q.size()) {
        U u = q[head++];
        topo_.push_back(u);
        for (U v : adj[u]) { lvl_[v] = std::max(lvl_[v], lvl_[u] + 1); if (--indeg[v] == 0) q.push_back(v); }
      }
      if (topo_.size() != alive_.size()) throw std::runtime_error("RefTimer: combinational loop");
    }
};

}  // namespace eco
