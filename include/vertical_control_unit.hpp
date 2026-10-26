#ifndef SIMPLERISC_VERTICAL_CONTROL_UNIT_HPP
#define SIMPLERISC_VERTICAL_CONTROL_UNIT_HPP

#include "types.hpp"
#include "alu.hpp"
#include <array>
#include <vector>
#include <string>

namespace simplerisc {

// Control Signals as detailed in Section 9.7.1 of Smruti Sarangi
struct BusSignals {
    bool pc_out{false};
    bool ir_out{false};
    bool regVal_out{false};
    bool aluResult_out{false};
    bool ldResult_out{false};

    bool pc_in{false};
    bool ir_in{false};
    bool regSrc_in{false};
    bool regData_in{false};
    bool A_in{false};
    bool B_in{false};
    bool mar_in{false};
    bool mdr_in{false};

    bool alu_trigger{false};
    ALUOp alu_op{ALUOp::ADD};
};

class VerticalControlUnit {
public:
    VerticalControlUnit();
    ~VerticalControlUnit() = default;

    void reset();

    // Load assembled 32-bit machine words into memory
    void loadProgram(const std::vector<word_t>& words, addr_t start_pc = 0);

    // Microprogram execution
    // Returns true when a macro-instruction completes (uPC returns to 0)
    bool stepMicroCycle();

    // Runs all micro-cycles for one macro-instruction (returns micro-cycle count)
    uint32_t stepInstruction();

    // Register file read & write
    word_t getRegister(reg_t r) const;
    void setRegister(reg_t r, word_t val);

    // Architectural PC & Flags
    addr_t getPC() const { return pc_; }
    Flags getFlags() const { return flags_; }

    // Internal Registers Access (Table 9.7)
    word_t getInternalReg(InternalReg reg) const;
    upc_t getUPC() const { return upc_; }
    word_t getSharedBus() const { return shared_bus_; }

    // Inspection
    std::string dumpInternalRegisters() const;
    std::string dumpMicroinstruction(upc_t addr) const;

private:
    void initMicrocode();

    // Architectural state
    std::array<word_t, NUM_REGISTERS> regs_;
    addr_t pc_{0};
    Flags flags_;
    std::array<word_t, 4096> mem_; // 16 KB local test memory

    // 19 Internal Registers (Table 9.7)
    std::array<word_t, NUM_INTERNAL_REGISTERS> internal_regs_;

    // Word-addressed Micro-PC
    upc_t upc_{0};

    // Shared Bus and Control Signals (Section 9.7.1)
    word_t shared_bus_{0};
    BusSignals signals_;

    // Microprogram Memory (Figure 9.24)
    std::vector<MicroInstruction> micro_rom_;
    std::array<upc_t, 32> opcode_switch_table_;
};

// Alias for backwards compatibility
using ControlUnit = VerticalControlUnit;

} // namespace simplerisc

#endif // SIMPLERISC_VERTICAL_CONTROL_UNIT_HPP
