#pragma once
#include "mock_timing_adapter.hpp"

namespace integration {
struct Fixture {
    eco::MockBackend design;
    FixedModel model;
};

// Combinational part of James's small ECO test design, ending at a primary
// output instead of a flop. Explicit educational delays; not an NLDM model.
// in1 -> u1(INV) -> u2(NAND2) -> out1; in2 -> u2.B
// in1 -> u3(INV) -> out2
inline Fixture make_fixture() {
    Fixture f;
    auto& m = f.design;
    m.defCell({"PORT_IN", "port", {}, {"P"}, false, false, true});
    m.defCell({"PORT_OUT", "port", {"P"}, {}, false, false, true});
    const auto inv = m.defCell({"INV_X1", "inv", {"A"}, {"ZN"}});
    const auto nand1 = m.defCell({"NAND2_X1", "nand2", {"A", "B"}, {"ZN"}});
    const auto nand2 = m.defCell({"NAND2_X2", "nand2", {"A", "B"}, {"ZN"}});
    const auto buf = m.defCell({"BUF_X1", "buf", {"A"}, {"Z"}, false, true});
    // Leave tombstoned pins before the live IDs to exercise explicit ID mapping.
    const auto retired = m.makeInst("retired", "INV_X1");
    m.removeInstance(retired);
    for (const auto* net : {"n_in1", "n_in2", "n1", "n2", "n3"}) m.makeNet(net);
    m.makeInst("in1", "PORT_IN"); m.makeInst("in2", "PORT_IN");
    m.makeInst("out1", "PORT_OUT"); m.makeInst("out2", "PORT_OUT");
    m.makeInst("u1", "INV_X1"); m.makeInst("u2", "NAND2_X1"); m.makeInst("u3", "INV_X1");
    const char* wires[][2] = {
        {"in1/P", "n_in1"}, {"u1/A", "n_in1"}, {"u3/A", "n_in1"},
        {"in2/P", "n_in2"}, {"u2/B", "n_in2"},
        {"u1/ZN", "n1"}, {"u2/A", "n1"},
        {"u2/ZN", "n2"}, {"out1/P", "n2"},
        {"u3/ZN", "n3"}, {"out2/P", "n3"}
    };
    for (const auto& wire : wires) m.connect(wire[0], wire[1]);
    f.model.cell_delay = {{inv, 0.2}, {nand1, 0.4}, {nand2, 0.25}, {buf, 0.05}};
    f.model.input_arrival = {{m.findPin("in1/P"), 0.1}, {m.findPin("in2/P"), 0.0}};
    f.model.required_time = {{m.findPin("out1/P"), 1.0}, {m.findPin("out2/P"), 0.8}};
    return f;
}
} // namespace integration
