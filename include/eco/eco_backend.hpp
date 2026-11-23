#pragma once
#include <string>
#include <vector>
#include "eco/eco_types.hpp"

namespace eco {
class EcoBackend {
    public:
        virtual ~EcoBackend() = default;

        // name lookup (hash maps live in the netlist); kInvalid if not found/alive
        virtual InstID findInst(const std::string& name) const = 0;
        virtual NetID findNet(const std::string& name) const = 0;
        virtual PinID findPin(const std::string& instSlashPin) const = 0;  // "u2/A"
        virtual CellID findCell(const std::string& name) const = 0;

        // queries
        virtual CellID cellOf(InstID) const = 0;
        virtual InstID instOf(PinID) const = 0;
        virtual bool isOutputPin(PinID) const = 0;
        virtual bool pinAlive(PinID) const = 0;
        virtual NetID netOf(PinID) const = 0;                             // kInvalid if floating
        virtual PinID driverOf(NetID) const = 0;                          // kInvalid if undriven
        virtual std::vector<PinID> sinksOf(NetID) const = 0;              // sorted by id
        virtual std::vector<PinID> inputPinsOf(InstID) const = 0;
        virtual std::vector<PinID> outputPinsOf(InstID) const = 0;
        virtual bool isPortNet(NetID) const = 0;                          // touches a primary port
        virtual bool swapCompatible(CellID from, CellID to) const = 0;    // same pins/directions/function
        virtual bool isBufferCell(CellID) const = 0;                      // 1 input, 1 output, non-inverting
        virtual bool isSequentialCell(CellID) const = 0;
        virtual std::vector<PinID> endpointPins() const = 0;              // PO pins + flop D pins
        virtual double clockPeriod() const = 0;

        // mutation primitives
        virtual void setCell(InstID, CellID) = 0;
        virtual void rewire(PinID, NetID) = 0;                            // kInvalid = disconnect
        virtual InstID addInstance(const std::string& name, CellID) = 0;  // pins created, unconnected
        virtual void removeInstance(InstID) = 0;                          // tombstone; pins must be disconnected
        virtual void restoreInstance(InstID) = 0;
        virtual NetID addNet(const std::string& name) = 0;
        virtual void removeNet(NetID) = 0;                                // tombstone; must be empty
        virtual void restoreNet(NetID) = 0;
        virtual void setClockPeriod(double) = 0;
};

}   //namespace eco