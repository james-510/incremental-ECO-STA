#include "samples.hpp"
#include <iomanip>
#include <iostream>
#include <string>

static void print_time(std::optional<sta::Time> t) {
  if (t) std::cout << std::fixed << std::setprecision(3) << *t;
  else std::cout << "NA";
}
int main(int argc, char** argv) {
  try {
    const std::string name = argc > 1 ? argv[1] : "budget_compare";
    const auto g = sta::sample(name);
    const auto r = sta::analyze_full(g);
    std::cout << "node,arrival_ns,required_ns,slack_ns,predecessor_arc\n";
    for (sta::NodeId v = 0; v < g.nodes.size(); ++v) {
      std::cout << g.nodes[v].name << ',';
      print_time(r.arrival[v]); std::cout << ',';
      print_time(r.required[v]); std::cout << ',';
      print_time(r.slack[v]); std::cout << ',';
      if (r.predecessor[v]) std::cout << *r.predecessor[v];
      else std::cout << "NA";
      std::cout << '\n';
    }
    const auto path = sta::worst_path(g, r);
    if (!path) std::cout << "worst_path=none\n";
    else {
      std::cout << "worst_path=";
      for (std::size_t i = 0; i < path->nodes.size(); ++i) {
        if (i) std::cout << " -> ";
        std::cout << g.nodes[path->nodes[i]].name;
      }
      std::cout << "; arrival_ns=" << path->arrival
                << "; required_ns=" << path->required
                << "; slack_ns=" << path->slack << '\n';
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
