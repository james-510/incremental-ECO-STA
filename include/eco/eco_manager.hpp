#pragma once
#include <cstddef>
#include <istream>
#include <string>
#include <vector>
#include "eco/eco_backend.hpp"
#include "eco/eco_types.hpp"

namespace eco {

class ECOManager {
    public:
        using Checkpoint = std::size_t;
        
        explicit ECOManager(EcoBackend& backend) : BE(backend) {}

    private:
        EcoBackend& BE;
        
};

}   // namespace eco