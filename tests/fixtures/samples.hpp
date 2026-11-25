#pragma once
#include "paths/path_enumerator.hpp"
#include <string>
namespace sta {
// Names: budget_compare, violations, missing_boundaries, ties, cycle.
TimingGraph sample(const std::string& name);
}
