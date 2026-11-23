#include <sstream>
#include <algorithm>
#include <unordered_set>
#include "eco/eco_manager.hpp"

namespace eco {
namespace {
    EcoResult fail(EcoStatus s, std::string msg) { return EcoResult{s, std::move(msg), {}}; }
    void sortUnique(std::vector<PinID>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
}   // namespace

// ------------------------- journal core ------------------------------
JournalOp ECOManager::inverseOf(const JournalOp& f) const {
    switch (f.kind) {
        case OpKind::SetCell:     return {OpKind::SetCell, f.a, BE_.cellOf(f.a), 0.0};
        case OpKind::Rewire:      return {OpKind::Rewire, f.a, BE_.netOf(f.a), 0.0};
        case OpKind::RemoveInst:  return {OpKind::RestoreInst, f.a};
        case OpKind::RestoreInst: return {OpKind::RemoveInst, f.a};
        case OpKind::RemoveNet:   return {OpKind::RestoreNet, f.a};
        case OpKind::RestoreNet:  return {OpKind::RemoveNet, f.a};
        case OpKind::SetPeriod:   return {OpKind::SetPeriod, kInvalid, kInvalid, BE_.clockPeriod()};
    }
    return f;
}

void ECOManager::doOp(const JournalOp& fwd) {
    journal_.push_back(inverseOf(fwd));  // capture old state before mutating
    apply(fwd);
}

void ECOManager::apply(const JournalOp& op) {
    switch (op.kind) {
        case OpKind::SetCell: {
            BE_.setCell(op.a, op.b);
            markInstance(op.a);
            markLoadDrivers(op.a);  // input pin caps changed -> upstream drivers see a new load
            break;
        }
        case OpKind::Rewire: {
            NetID oldNet = BE_.netOf(op.a);
            BE_.rewire(op.a, op.b);
            markRewire(op.a, oldNet, op.b);
            structural_ = true;
            break;
        }
        case OpKind::RemoveInst: {
            BE_.removeInstance(op.a);  
            markInstance(op.a); 
            structural_ = true; 
            break;
        }
        case OpKind::RestoreInst: {
            BE_.restoreInstance(op.a); 
            markInstance(op.a); 
            structural_ = true; 
            break;
        }
        case OpKind::RemoveNet: {
            BE_.removeNet(op.a);   
            break;
        }
        case OpKind::RestoreNet: {
            BE_.restoreNet(op.a);  
            break;
        }
        case OpKind::SetPeriod: {
            BE_.setClockPeriod(op.c); 
            markEndpoints(); 
            break;
        }
    }
}

void ECOManager::undoTo(Checkpoint cp) {
    while (journal_.size() > cp) {
        JournalOp op = journal_.back();
        journal_.pop_back();
        apply(op);  // undo records are ordinary primitives (seeds are marked the same way)
    }
}

// ------------------------ dirty seed marking -----------------------------
void ECOManager::markArrival(PinID p) { 
    if (p != kInvalid) { arr_.push_back(p); }
}
void ECOManager::markRequired(PinID p) { 
    if (p != kInvalid) { req_.push_back(p); }
}

void ECOManager::markInstance(InstID i) {
    for (PinID p : BE_.outputPinsOf(i)) {
        markArrival(p);     // arcs through the cell changed
    }
    for (PinID p : BE_.inputPinsOf(i)) {
        markRequired(p);    // required times flow back through them
    }
}

void ECOManager::markLoadDrivers(InstID i) {
    for (PinID p : BE_.inputPinsOf(i)) {
        NetID n = BE_.netOf(p);
        if (n == kInvalid) {
            continue;
        }
        PinID d = BE_.driverOf(n);
        markArrival(d);   // driver's delay/slew depend on load
        markRequired(d);
    }
}

void ECOManager::markRewire(PinID pin, NetID oldNet, NetID newNet) {
    markArrival(pin);
    markRequired(pin);
    for (NetID n : {oldNet, newNet}) {
        if (n == kInvalid) {
            continue;
        }
        PinID d = BE_.driverOf(n);
        markArrival(d);    // net load changed
        markRequired(d);   // fanout set changed
        if (BE_.isOutputPin(pin)) {
            for (PinID s : BE_.sinksOf(n)) {
                markArrival(s);     // every sink sees a new arrival if net gains/loses its driver
            }
        }
    }
}

void ECOManager::markEndpoints() {
    for (PinID e : BE_.endpointPins()) {
        markRequired(e);    // only required times depend on the period
    }
}

DirtySeeds ECOManager::takeSeeds() {
    DirtySeeds s;
    // deduplicate pins and remove any that are no longer alive (e.g. removed by a buffer)
    auto keep = [&](std::vector<PinID>& src, std::vector<PinID>& dst) {
        sortUnique(src);
        for (PinID p : src) {
            if (BE_.pinAlive(p)) dst.push_back(p);
        }
        src.clear();
    };
    keep(arr_, s.arrival);
    keep(req_, s.requirement);
    s.structural = structural_;
    structural_ = false;
    return s;
}

// ---------------------------- helper functions -----------------------------
// generate a unique name for a new net or instance (not already in the netlist)
std::string ECOManager::uniqueName(const std::string& base) const {
    unsigned k = counter_;
    while (true) {
        std::string n = base + std::to_string(k++);
        if (BE_.findInst(n + "_i") == kInvalid && BE_.findNet(n) == kInvalid) {
            return n;
        }
    }
}

// reconnecting `sinkPin` (on instance X) to `newNet` (driven by instance T) closes a
// combinational loop iff T is combinationally reachable from X (flops cut the path)
bool ECOManager::wouldCreateCycle(PinID sinkPin, NetID newNet) const {
    PinID drv = BE_.driverOf(newNet);
    if (drv == kInvalid) {
        return false;
    }
    InstID X = BE_.instOf(sinkPin), T = BE_.instOf(drv);
    if (BE_.isSequentialCell(BE_.cellOf(X)) || BE_.isSequentialCell(BE_.cellOf(T))) {
        return false;
    }
    if (X == T) {
        return true;
    }
    
    std::vector<InstID> stack{X};
    std::unordered_set<InstID> seen{X};
    // DFS through the combinational fanout of X, looking for T
    while (!stack.empty()) {
        InstID u = stack.back();
        stack.pop_back();
        for (PinID o : BE_.outputPinsOf(u)) {
            NetID n = BE_.netOf(o);
            if (n == kInvalid) {
                continue;
            }
            for (PinID s : BE_.sinksOf(n)) {
                InstID v = BE_.instOf(s);
                if (v == T) { 
                    return true; 
                }
                if (BE_.isSequentialCell(BE_.cellOf(v))) { 
                    continue; 
                }
                if (seen.insert(v).second) { 
                    stack.push_back(v); 
                }
            }
        }
    }
    return false;
}

// ----------------------- edit functions -----------------------------
EcoResult ECOManager::swapCell(const std::string& instName, const std::string& cellName) {
    InstID inst = BE_.findInst(instName);
    if (inst == kInvalid) {
        return fail(EcoStatus::UnknownInstance, "unknown instance " + instName);
    }
    CellID cell = BE_.findCell(cellName);
    if (cell == kInvalid) {
        return fail(EcoStatus::UnknownCell, "unknown cell " + cellName);
    }
    CellID curr = BE_.cellOf(inst);
    if (curr == cell) {
        return EcoResult{};  // same cell, no dirty seeds
    }
    if (!BE_.swapCompatible(curr, cell)) {
        return fail(EcoStatus::IncompatibleCell, cellName + " is not pin/function compatible with the current cell of " + instName);
    }
    doOp({OpKind::SetCell, inst, cell});
    return EcoResult{};
}

EcoResult ECOManager::insertBuffer(const std::string& netName, const std::string& bufCellName,
                                   const std::vector<std::string>& sinkNames) {
    NetID net = BE_.findNet(netName);
    if (net == kInvalid) {
        return fail(EcoStatus::UnknownNet, "unknown net " + netName);
    }
    CellID cell = BE_.findCell(bufCellName);
    if (cell == kInvalid) {
        return fail(EcoStatus::UnknownCell, "unknown cell " + bufCellName);
    }
    if (!BE_.isBufferCell(cell)) {
        return fail(EcoStatus::NotABuffer, bufCellName + " is not a buffer");
    }

    // determine which sinks to buffer (all of them if sinkNames is empty)
    std::vector<PinID> netSinks = BE_.sinksOf(net), sinks;
    if (sinkNames.empty()) {
        sinks = netSinks;
    } 
    else {
        for (const auto& sn : sinkNames) {
            PinID p = BE_.findPin(sn);
            if (p == kInvalid) {
                return fail(EcoStatus::UnknownPin, "unknown pin " + sn);
            }
            if (std::find(netSinks.begin(), netSinks.end(), p) == netSinks.end()) {
                return fail(EcoStatus::SinkNotOnNet, sn + " is not a sink of " + netName);
            }
            sinks.push_back(p);
        }
        sortUnique(sinks);
    }
    if (sinks.empty()) {
        return fail(EcoStatus::EmptySinkList, "no sinks to buffer on " + netName);
    }

    // create a new net and buffer instance, then rewire the old net 
    // to the buffer input and the new net to the buffer output
    std::string base = uniqueName("eco_buf_");
    ++counter_;
    NetID newNet = BE_.addNet(base);
    journal_.push_back({OpKind::RemoveNet, newNet});
    InstID buf = BE_.addInstance(base + "_i", cell);
    journal_.push_back({OpKind::RemoveInst, buf});
    markInstance(buf);
    structural_ = true;

    PinID bin = BE_.inputPinsOf(buf).front(), bout = BE_.outputPinsOf(buf).front();
    doOp({OpKind::Rewire, bin, net});
    doOp({OpKind::Rewire, bout, newNet});
    for (PinID s : sinks) {
        doOp({OpKind::Rewire, s, newNet});
    }

    EcoResult r;
    r.created = base + "_i";
    return r;
}

EcoResult ECOManager::removeBuffer(const std::string& instName) {
    InstID buf = BE_.findInst(instName);
    if (buf == kInvalid) {
        return fail(EcoStatus::UnknownInstance, "unknown instance " + instName);
    }
    if (!BE_.isBufferCell(BE_.cellOf(buf))) {
        return fail(EcoStatus::NotABuffer, instName + " is not a buffer");
    }
    // check that the buffer is fully connected and not driving a port net
    PinID bin = BE_.inputPinsOf(buf).front(), bout = BE_.outputPinsOf(buf).front();
    NetID A = BE_.netOf(bin), B = BE_.netOf(bout);
    if (A == kInvalid || B == kInvalid) { 
        return fail(EcoStatus::Unconnected, instName + " has a floating pin");
    }
    if (BE_.isPortNet(B)) { 
        return fail(EcoStatus::PortNetProtected, "output net of " + instName + " is connected to a port");
    }

    for (PinID s : BE_.sinksOf(B)) {
        doOp({OpKind::Rewire, s, A});  // sinks move to the upstream net
    }
    doOp({OpKind::Rewire, bin, kInvalid});
    doOp({OpKind::Rewire, bout, kInvalid});
    doOp({OpKind::RemoveInst, buf});
    doOp({OpKind::RemoveNet, B});
    return EcoResult{};
}

EcoResult ECOManager::reconnect(const std::string& pinName, const std::string& netName) {
    PinID pin = BE_.findPin(pinName);
    if (pin == kInvalid) { 
        return fail(EcoStatus::UnknownPin, "unknown pin " + pinName);
    }
    NetID net = BE_.findNet(netName);
    if (net == kInvalid) { 
        return fail(EcoStatus::UnknownNet, "unknown net " + netName);
    }
    if (BE_.isOutputPin(pin)) { 
        return fail(EcoStatus::PinNotASink, pinName + " is an output pin");
    }
    if (BE_.netOf(pin) == net) { 
        return EcoResult{};     // pin already connected to target net
    }
    if (wouldCreateCycle(pin, net)) {
        return fail(EcoStatus::WouldCreateCycle, "reconnecting " + pinName + " to " + netName + " creates a combinational loop");
    }
    doOp({OpKind::Rewire, pin, net});
    return EcoResult{};
}

EcoResult ECOManager::setClockPeriod(double p) {
    if (!(p > 0.0)) {
        return fail(EcoStatus::BadPeriod, "clock period must be positive");
    }
    if (p == BE_.clockPeriod()) { 
        return EcoResult{};     // no change to clock period
    }
    JournalOp op{OpKind::SetPeriod};
    op.c = p;
    doOp(op);
    return EcoResult{};
}

// ---------------------- text interface ----------------------
EcoResult ECOManager::applyCommand(const std::string& line) {
    // parse the line into whitespace-separated tokens
    std::istringstream ss(line);
    std::vector<std::string> t;
    for (std::string w; ss >> w;) { 
        t.push_back(w); 
    }
    if (t.empty() || t[0][0] == '#') { 
        return EcoResult{}; // blank or comment line == no edits
    }
    const std::string& c = t[0];
    auto bad = [&](const char* usage) { 
        return fail(EcoStatus::BadCommand, std::string("usage: ") + usage); 
    };

    if (c == "swap_cell") { 
        return t.size() == 3 ? swapCell(t[1], t[2]) : bad("swap_cell <inst> <cell>");
    }
    if (c == "remove_buffer") { 
        return t.size() == 2 ? removeBuffer(t[1]) : bad("remove_buffer <inst>");
    }
    if (c == "reconnect") { 
        return t.size() == 3 ? reconnect(t[1], t[2]) : bad("reconnect <inst/pin> <net>");
    }
    if (c == "insert_buffer") {
        // must have at least 3 tokens: command, net, buffer cell
        if (t.size() < 3) { 
            return bad("insert_buffer <net> <buf_cell> [<inst/pin> ...]");
        }
        return insertBuffer(t[1], t[2], std::vector<std::string>(t.begin() + 3, t.end()));
    }
    if (c == "set_clock_period") {
        // must have exactly 2 tokens: command, value
        if (t.size() != 2) { 
            return bad("set_clock_period <value>");
        }
        try { return setClockPeriod(std::stod(t[1])); }
        catch (...) { return fail(EcoStatus::BadCommand, "bad number: " + t[1]); }
    }
    return fail(EcoStatus::BadCommand, "unknown command " + c);
}

EcoResult ECOManager::applyScript(std::istream& in, std::size_t* applied) {
    std::size_t n = 0, lineNo = 0;
    std::string line;
    // apply each line in the script, stopping at the first failure
    while (std::getline(in, line)) {
        ++lineNo;
        std::size_t first = line.find_first_not_of(" \t\r");
        bool blank = (first == std::string::npos) || line[first] == '#';
        EcoResult r = applyCommand(line);
        // if a line fails, return the error and how many edits were applied before it
        if (!r.ok()) {
            r.message = "line " + std::to_string(lineNo) + ": " + r.message;
            if (applied) {
                *applied = n;
            }
            return r;
        }
        // count the number of non-blank lines that were successfully applied
        if (!blank) {
            ++n;
        }
    }
    if (applied) { 
        *applied = n;
    }
    return EcoResult{};
}

}  // namespace eco