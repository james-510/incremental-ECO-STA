#pragma once
#include <cstdint>

namespace common {
// Shared design IDs. Keep removed slots while undo/timing can refer to them.
using CellId = std::uint32_t;
using InstId = std::uint32_t;
using PinId = std::uint32_t;
using NetId = std::uint32_t;
inline constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
} // namespace common
