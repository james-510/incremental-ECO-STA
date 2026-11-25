// Tiny in-memory netlist implementing EcoBackend. For developing/testing ECOManager before the
// real netlist/timing graph adapters exist. Deliberately simple (linear scans), NOT fast.
// Ports are modeled as pseudo-instances (PORT_IN has output pin "P", PORT_OUT has input pin "P").
#pragma once
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include "eco/eco_backend.hpp"

namespace eco {

struct MockCellDef {
    std::string name, footprint;
    std::vector<std::string> in, out;
    bool seq = false, buf = false, port = false;
};

class MockBackend : public EcoBackend {
    public:
        using U = std::uint32_t;
        static constexpr U kNil = kInvalid;

        // ---------- building (bypasses the ECO journal)
        CellID defCell(MockCellDef c) {
            cells_.push_back(c);
            cellByName_[c.name] = static_cast<CellID>(cells_.size() - 1);
            return static_cast<CellID>(cells_.size() - 1);
        }
        NetID makeNet(const std::string& n) { return addNet(n); }
        InstID makeInst(const std::string& n, const std::string& cell) { return addInstance(n, findCell(cell)); }
        void connect(const std::string& pin, const std::string& net) { rewire(findPin(pin), findNet(net)); }

        // ---------- test helpers
        std::size_t numPins() const { return pins_.size(); }
        std::size_t numInsts() const { return insts_.size(); }
        std::size_t numNets() const { return nets_.size(); }
        bool netAlive(NetID n) const { return n < nets_.size() && nets_[n].alive; }
        const MockCellDef& cellDef(CellID c) const { return cells_[c]; }
        std::vector<InstID> aliveInsts() const {
            std::vector<InstID> r;
            for (InstID i = 0; i < insts_.size(); ++i)
                if (insts_[i].alive) r.push_back(i);
            return r;
        }
        std::vector<PinID> allAlivePins() const {
            std::vector<PinID> r;
            for (PinID p = 0; p < pins_.size(); ++p)
                if (insts_[pins_[p].inst].alive) r.push_back(p);
            return r;
        }
        std::string pinBaseName(PinID p) const { return pins_[p].name; }

        std::string snapshot() const {
            std::vector<std::string> L;
            for (const auto& i : insts_) {
                if (!i.alive) continue;
                L.push_back("inst " + i.name + " " + cells_[i.cell].name);
                for (PinID p : i.pins) {
                    L.push_back("pin " + i.name + "/" + pins_[p].name + " -> " + (pins_[p].net == kInvalid ? "-" : nets_[pins_[p].net].name));
                }
            }
            for (const auto& n : nets_) {
                if (n.alive) {
                    L.push_back("net " + n.name);
                }
            }
            L.push_back("period " + std::to_string(period_));
            std::sort(L.begin(), L.end());
            std::string s;
            for (auto& l : L) {
                s += l + "\n";
            }
            return s;
        }
        // "" if consistent, else a description of the first violation
        std::string invariants() const {
            for (const auto& i : insts_) {
                if (!i.alive) continue;
                for (PinID p : i.pins) {
                    if (pins_[p].net != kInvalid && !nets_[pins_[p].net].alive) {
                        return "pin " + i.name + "/" + pins_[p].name + " on dead net";
                    }
                }
            }
            for (NetID n = 0; n < nets_.size(); ++n) {
                if (!nets_[n].alive) continue;
                int drivers = 0;
                for (const auto& p : pins_) {
                    if (p.net == n && p.isOut && insts_[p.inst].alive) {
                        ++drivers;
                    }
                }
                if (drivers > 1) {
                    return "net " + nets_[n].name + " has multiple drivers";
                }
            }
            return {};
        }
        bool acyclic() const {  // no combinational loop (edges into flops are cut)
            size_t N = insts_.size();
            std::vector<std::vector<InstID>> adj(N);
            std::vector<int> indeg(N, 0);
            for (NetID n = 0; n < nets_.size(); ++n) {
                if (!nets_[n].alive) continue;
                PinID d = driverOf(n);
                if (d == kInvalid) continue;
                for (PinID s : sinksOf(n)) {
                    InstID x = pins_[s].inst;
                    if (cells_[insts_[x].cell].seq) continue;
                    adj[pins_[d].inst].push_back(x);
                    ++indeg[x];
                }
            }
            std::vector<InstID> q;
            size_t alive = 0;
            for (InstID i = 0; i < N; ++i) {
                if (insts_[i].alive) { 
                    ++alive; 
                    if (!indeg[i]) q.push_back(i);
                }
            }
            size_t seen = 0;
            while (!q.empty()) {
                InstID u = q.back(); q.pop_back(); ++seen;
                for (InstID v : adj[u]) {
                    if (--indeg[v] == 0) q.push_back(v);
                }
            }
            return seen == alive;
        }
        std::string pinName(PinID p) const { return insts_[pins_[p].inst].name + "/" + pins_[p].name; }
        std::vector<std::string> aliveInstNames() const { std::vector<std::string> r; for (auto& i : insts_) if (i.alive) r.push_back(i.name); return r; }
        std::vector<std::string> aliveNetNames() const { std::vector<std::string> r; for (auto& n : nets_) if (n.alive) r.push_back(n.name); return r; }
        std::vector<std::string> cellNames() const { std::vector<std::string> r; for (auto& c : cells_) if (!c.port) r.push_back(c.name); return r; }
        std::vector<std::string> sinkPinNames(const std::string& net) const { std::vector<std::string> r; for (PinID p : sinksOf(findNet(net))) r.push_back(pinName(p)); return r; }
        std::vector<std::string> inputPinNames(const std::string& inst) const { std::vector<std::string> r; for (PinID p : inputPinsOf(findInst(inst))) r.push_back(pinName(p)); return r; }
        std::vector<std::string> bufferInstNames() const { std::vector<std::string> r; for (auto& i : insts_) if (i.alive && cells_[i.cell].buf) r.push_back(i.name); return r; }
        std::vector<std::string> bufferCellNames() const { std::vector<std::string> r; for (const auto& c : cells_) if (c.buf) r.push_back(c.name); return r; }
        std::vector<std::string> swapCandidates(const std::string& inst) const {
            std::vector<std::string> r;
            InstID i = findInst(inst);
            if (i == kInvalid) return r;
            CellID source = cellOf(i);
            for (CellID c = 0; c < cells_.size(); ++c)
                if (swapCompatible(source, c)) r.push_back(cells_[c].name);
            return r;
        }

        // ---------- EcoBackend
        InstID findInst(const std::string& n) const override { auto it = instByName_.find(n); return it == instByName_.end() ? kInvalid : it->second; }
        NetID findNet(const std::string& n) const override { auto it = netByName_.find(n); return it == netByName_.end() ? kInvalid : it->second; }
        CellID findCell(const std::string& n) const override { auto it = cellByName_.find(n); return it == cellByName_.end() ? kInvalid : it->second; }
        PinID findPin(const std::string& s) const override {
            auto slash = s.find('/');
            if (slash == std::string::npos) return kInvalid;
            InstID i = findInst(s.substr(0, slash));
            if (i == kInvalid) return kInvalid;
            for (PinID p : insts_[i].pins) {
                if (pins_[p].name == s.substr(slash + 1)) return p;
            }
            return kInvalid;
        }
        CellID cellOf(InstID i) const override { return insts_[i].cell; }
        InstID instOf(PinID p) const override { return pins_[p].inst; }
        bool isOutputPin(PinID p) const override { return pins_[p].isOut; }
        bool pinAlive(PinID p) const override { return insts_[pins_[p].inst].alive; }
        NetID netOf(PinID p) const override { return pins_[p].net; }
        PinID driverOf(NetID n) const override {
            for (PinID p = 0; p < pins_.size(); ++p) {
                if (pins_[p].net == n && pins_[p].isOut && insts_[pins_[p].inst].alive) return p;
            }
            return kInvalid;
        }
        std::vector<PinID> sinksOf(NetID n) const override {
            std::vector<PinID> r;
            for (PinID p = 0; p < pins_.size(); ++p) {
                if (pins_[p].net == n && !pins_[p].isOut && insts_[pins_[p].inst].alive) r.push_back(p);
            }
            return r;
        }
        std::vector<PinID> inputPinsOf(InstID i) const override { std::vector<PinID> r; for (PinID p : insts_[i].pins) if (!pins_[p].isOut) r.push_back(p); return r; }
        std::vector<PinID> outputPinsOf(InstID i) const override { std::vector<PinID> r; for (PinID p : insts_[i].pins) if (pins_[p].isOut) r.push_back(p); return r; }
        bool isPortNet(NetID n) const override {
            for (const auto& p : pins_) {
                if (p.net == n && insts_[p.inst].alive && cells_[insts_[p.inst].cell].port) return true;
            }
            return false;
        }
        bool swapCompatible(CellID a, CellID b) const override {
            const auto &x = cells_[a], &y = cells_[b];
            return !x.port && !y.port && x.footprint == y.footprint && x.in == y.in && x.out == y.out && x.seq == y.seq;
        }
        bool isBufferCell(CellID c) const override { return cells_[c].buf; }
        bool isSequentialCell(CellID c) const override { return cells_[c].seq; }
        std::vector<PinID> endpointPins() const override {
            std::vector<PinID> r;
            for (PinID p = 0; p < pins_.size(); ++p) {
                const auto& i = insts_[pins_[p].inst];
                if (!i.alive || pins_[p].isOut) continue;
                const auto& c = cells_[i.cell];
                if ((c.port) || (c.seq && pins_[p].name == "D")) r.push_back(p);
            }
            return r;
        }
        double clockPeriod() const override { return period_; }

        void setCell(InstID i, CellID c) override { insts_[i].cell = c; }
        void rewire(PinID p, NetID n) override { pins_[p].net = n; }
        InstID addInstance(const std::string& name, CellID c) override {
            InstID id = static_cast<InstID>(insts_.size());
            insts_.push_back({name, c, true, {}});
            for (const auto& pn : cells_[c].in) { 
                pins_.push_back({id, pn, false, kInvalid}); 
                insts_[id].pins.push_back(static_cast<PinID>(pins_.size() - 1)); 
            }
            for (const auto& pn : cells_[c].out) { 
                pins_.push_back({id, pn, true, kInvalid}); 
                insts_[id].pins.push_back(static_cast<PinID>(pins_.size() - 1)); 
            }
            instByName_[name] = id;
            return id;
        }
        void removeInstance(InstID i) override { insts_[i].alive = false; instByName_.erase(insts_[i].name); }
        void restoreInstance(InstID i) override { insts_[i].alive = true; instByName_[insts_[i].name] = i; }
        NetID addNet(const std::string& name) override {
            nets_.push_back({name, true});
            netByName_[name] = static_cast<NetID>(nets_.size() - 1);
            return static_cast<NetID>(nets_.size() - 1);
        }
        void removeNet(NetID n) override { nets_[n].alive = false; netByName_.erase(nets_[n].name); }
        void restoreNet(NetID n) override { nets_[n].alive = true; netByName_[nets_[n].name] = n; }
        void setClockPeriod(double p) override { period_ = p; }

    private:
        struct Inst { 
            std::string name; 
            CellID cell; 
            bool alive; 
            std::vector<PinID> pins; 
        };
        struct Pin { 
            InstID inst; 
            std::string name; 
            bool isOut; 
            NetID net; 
        };
        struct Net { 
            std::string name; 
            bool alive; 
        };
        std::vector<MockCellDef> cells_;
        std::vector<Inst> insts_;
        std::vector<Pin> pins_;
        std::vector<Net> nets_;
        std::unordered_map<std::string, CellID> cellByName_;
        std::unordered_map<std::string, InstID> instByName_;
        std::unordered_map<std::string, NetID> netByName_;
        double period_ = 1.0;
};

}  // namespace eco