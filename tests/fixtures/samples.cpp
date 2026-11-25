#include "samples.hpp"
#include <stdexcept>
namespace sta {
TimingGraph sample(const std::string& name) {
  using std::nullopt;
  if (name == "budget_compare" || name == "violations") {
    TimingGraph g;
    // IDs: a=0, b=1, x=2, z=3, y1=4, y2=5. Units: ns.
    // x represents reconvergence; these abstract vertices can be pin names
    // when your graph builder becomes available.
    g.nodes = {{"a", 1.0, nullopt}, {"b", 0.0, nullopt},
               {"x", nullopt, nullopt}, {"z", nullopt, nullopt},
               {"y1", nullopt, 8.0}, {"y2", nullopt, 4.5}};
    g.arcs = {{0, 2, 2.0}, {1, 2, 4.0}, {2, 4, 3.0},
              {0, 3, 1.0}, {3, 5, 2.0}};
    if (name == "violations") {
      g.nodes[4].required_time = 6.0;
      g.nodes[5].required_time = 3.9;
    }
    return g;
  }
  if (name == "missing_boundaries")
    return {{{"a", 0.0, nullopt}, {"unconstrained", nullopt, nullopt},
             {"uninitialized", nullopt, nullopt}, {"unreachable", nullopt, 5.0}},
            {{0, 1, 2.0}, {2, 3, 1.0}}};
  if (name == "ties")
    return {{{"a", 0.0, nullopt}, {"b", 0.0, nullopt},
             {"out", nullopt, 2.0}}, {{0, 2, 1.0}, {1, 2, 1.0}}};
  if (name == "cycle")
    return {{{"u", nullopt, nullopt}, {"v", nullopt, nullopt}},
            {{0, 1, 1.0}, {1, 0, 1.0}}};
  throw std::invalid_argument("unknown sample: " + name);
}
}
