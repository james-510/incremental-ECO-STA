#pragma once
#include <cstddef>
#include <istream>
#include <string>
#include <vector>
#include "eco/eco_backend.hpp"
#include "eco/eco_types.hpp"

namespace eco {

class ECOManager {
    private:
        EcoBackend& BE_;
        std::vector<JournalOp> journal_;
        std::vector<PinID> arr_, req_;
        bool structural_ = false;
        unsigned counter_ = 0;
        
        void doOp(const JournalOp& fwd);   // journal inverse, then apply
        void apply(const JournalOp& op);   // execute + mark dirty
        JournalOp inverseOf(const JournalOp& fwd) const;

        void markInstance(InstID i);
        void markLoadDrivers(InstID i);
        void markRewire(PinID pin, NetID oldNet, NetID newNet);
        void markEndpoints();
        void markArrival(PinID p);
        void markRequired(PinID p);

        bool wouldCreateCycle(PinID sinkPin, NetID newNet) const;
        std::string uniqueName(const std::string& base) const;

    public:
        using Checkpoint = std::size_t;
        
        explicit ECOManager(EcoBackend& backend) : BE_(backend) {}

        // edits (validate first, nothing is changed on failure)
        EcoResult swapCell(const std::string& inst, const std::string& newCell);
        // sinkPins empty = buffer every sink of the net
        EcoResult insertBuffer(const std::string& net, const std::string& bufCell,
                               const std::vector<std::string>& sinkPins = {});
        EcoResult removeBuffer(const std::string& inst);
        EcoResult reconnect(const std::string& pin, const std::string& net);
        EcoResult setClockPeriod(double period);

        // --- text interface for the CLI (--eco edits.txt) ---
        //   swap_cell <inst> <cell>
        //   insert_buffer <net> <buf_cell> [<inst/pin> ...]
        //   remove_buffer <inst>
        //   reconnect <inst/pin> <net>
        //   set_clock_period <value>
        EcoResult applyCommand(const std::string& line);
        // stops at the first failing line and edits before applied (undoTo a checkpoint to revert)
        EcoResult applyScript(std::istream& in, std::size_t* applied = nullptr);

        // undo/commit
        Checkpoint checkpoint() const { return journal_.size(); }
        void undoTo(Checkpoint cp);
        void undoAll() { undoTo(0); }
        void commit() { journal_.clear(); }

        // handoff to the timing engine (accumulates across edits AND undos)
        DirtySeeds takeSeeds();
};

}   // namespace eco