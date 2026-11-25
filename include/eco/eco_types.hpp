#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "common/ids.hpp"

namespace eco {

// Typedef IDs
using CellID = common::CellId;
using InstID = common::InstId;
using PinID = common::PinId;
using NetID = common::NetId;
inline constexpr auto kInvalid = common::kInvalid;

// ECO error codes
enum class EcoStatus {
    Ok,
    UnknownInstance, 
    UnknownCell, 
    UnknownNet, 
    UnknownPin,
    IncompatibleCell,   // swap target differs in pins/function
    NotABuffer,         // insert/remove_buffer on a non-buffer cell
    PinNotASink,        // reconnect target is an output pin
    SinkNotOnNet,       // insert_buffer sink is not a sink of the net
    EmptySinkList,      // nothing to buffer
    Unconnected,        // remove_buffer on a buffer with a floating input/output
    PortNetProtected,   // remove_buffer would delete a port-connected net
    WouldCreateCycle,   // reconnect would create a combinational loop
    BadPeriod,
    BadCommand
};

struct EcoResult {
    EcoStatus status = EcoStatus::Ok;
    std::string message;
    std::string created;    // if insert_buffer, name of instance created by it
    bool ok() const {
        return status == EcoStatus::Ok;
    }
};

// Timing engine recomputes with dirty seeds (modified pin, cell, etc.)
struct DirtySeeds {
    std::vector<PinID> arrival;         // recompute downstream delay/arrival/skew here (forward pass)
    std::vector<PinID> requirement;     // recompute upstream required time here (backward pass)
    bool structural = false;
    bool empty() const {
        return arrival.empty() && requirement.empty();
    }
};

// Journal UNDO records for reversing an ECO modification
enum class OpKind {
    SetCell,
    Rewire,
    RemoveInst,
    RestoreInst,
    RemoveNet,
    RestoreNet,
    SetPeriod
};

struct JournalOp {
    OpKind kind;
    std::uint32_t a = kInvalid;  // inst / pin / net
    std::uint32_t b = kInvalid;  // cell / net
    double c = 0.0;              // clock period
};

}   //namespace eco
