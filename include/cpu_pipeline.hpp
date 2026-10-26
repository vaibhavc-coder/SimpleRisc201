#ifndef SIMPLERISC_CPU_PIPELINE_HPP
#define SIMPLERISC_CPU_PIPELINE_HPP

#include "types.hpp"
#include "alu.hpp"
#include "vertical_control_unit.hpp"
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <set>

namespace simplerisc {

enum class PipelineMode {
    FOUR_STAGE,       // 4-Stage: IF -> ID/RR -> EX -> MEM/WB
    SIX_STAGE,        // 6-Stage Super-Pipeline: IF1 -> IF2 -> ID -> RR -> EX -> WB
    MICROPROGRAMMED   // Microprogrammed Shared-Bus Model (Sarangi Ch. 9)
};

enum class ForwardSource {
    NONE,
    EX_MEM,
    MEM_WB
};

struct PipelineMetrics {
    uint64_t total_cycles{0};         // Total execution clock / micro-cycles
    uint64_t instructions_retired{0}; // Total instructions completed writeback
    uint64_t stall_cycles{0};         // Load-use and hazard bubble stall cycles
    uint64_t branch_flushes{0};       // Branch misprediction / taken flush events
    uint64_t forwarding_events{0};     // Operand bypass forwarding occurrences
    uint64_t load_use_hazards{0};     // Detected load-use hazard events

    double getCPI() const {
        return (instructions_retired > 0) ? static_cast<double>(total_cycles) / instructions_retired : 0.0;
    }
    double getIPC() const {
        return (total_cycles > 0) ? static_cast<double>(instructions_retired) / total_cycles : 0.0;
    }

    std::string dump() const;
};

// ----------------------------------------------------------------------------
// 4-Stage Pipeline Inter-Stage Registers
// ----------------------------------------------------------------------------
struct Latch_IF_ID {
    addr_t pc{0};
    word_t raw_inst{0};
    bool valid{false};
    bool bubble{false};
};

struct Latch_ID_EX {
    addr_t pc{0};
    Instruction inst;
    word_t val_rs1{0};
    word_t val_rs2{0};
    word_t val_rd{0};
    bool valid{false};
    bool bubble{false};
};

struct Latch_EX_MEM {
    addr_t pc{0};
    Instruction inst;
    word_t alu_result{0};
    word_t store_data{0};
    bool branch_taken{false};
    addr_t branch_target{0};
    ForwardSource fwd_a{ForwardSource::NONE};
    ForwardSource fwd_b{ForwardSource::NONE};
    bool valid{false};
    bool bubble{false};
};

struct Latch_MEM_WB {
    addr_t pc{0};
    Instruction inst;
    word_t write_data{0};
    bool valid{false};
    bool bubble{false};
};

// ----------------------------------------------------------------------------
// 6-Stage Super-Pipeline Inter-Stage Registers
// ----------------------------------------------------------------------------
struct Latch_IF1_IF2 {
    addr_t pc{0};
    bool valid{false};
    bool bubble{false};
};

struct Latch_IF2_ID {
    addr_t pc{0};
    word_t raw_inst{0};
    bool valid{false};
    bool bubble{false};
};

struct Latch_ID_RR {
    addr_t pc{0};
    Instruction inst;
    bool valid{false};
    bool bubble{false};
};

struct Latch_RR_EX {
    addr_t pc{0};
    Instruction inst;
    word_t val_rs1{0};
    word_t val_rs2{0};
    word_t val_rd{0};
    bool valid{false};
    bool bubble{false};
};

// ----------------------------------------------------------------------------
// CPUPipeline Class
// ----------------------------------------------------------------------------
class CPUPipeline {
public:
    explicit CPUPipeline(PipelineMode mode = PipelineMode::FOUR_STAGE);
    virtual ~CPUPipeline() = default;

    void reset();
    void loadProgram(const std::vector<word_t>& words, addr_t text_start = TEXT_BASE);

    // Mode management
    void setPipelineMode(PipelineMode mode);
    PipelineMode getPipelineMode() const { return mode_; }

    // Execution methods
    bool cycleStep(); // Execute 1 cycle in active mode
    bool macroStep(); // Execute until 1 instruction completes WB or halts
    void run(uint64_t max_cycles = 10000000);

    // State queries
    bool isHalted() const { return halted_; }
    void halt() { halted_ = true; }
    bool hasException() const { return has_exception_; }
    std::string getExceptionMessage() const { return exception_msg_; }

    word_t getRegister(reg_t r) const;
    void setRegister(reg_t r, word_t val);

    addr_t getPC() const { return pc_; }
    void setPC(addr_t pc) { pc_ = pc; }

    Flags getFlags() const { return flags_; }
    void setFlags(const Flags& flags) { flags_ = flags; }

    word_t readMemory(addr_t addr) const;
    void writeMemory(addr_t addr, word_t val);

    const PipelineMetrics& getMetrics() const { return metrics_; }
    void resetMetrics() { metrics_ = PipelineMetrics{}; }

    // Visualizers
    std::string dumpPipelineState() const;
    std::string dump4StageState() const;
    std::string dump6StageState() const;
    std::string dumpRegisters() const;

private:
    bool step4StageCycle();
    bool step6StageCycle();
    bool stepMicroCycle();

    bool is4StageEmpty() const;
    bool is6StageEmpty() const;

    PipelineMode mode_{PipelineMode::FOUR_STAGE};

    // Architectural state
    std::array<word_t, NUM_REGISTERS> regs_;
    addr_t pc_{TEXT_BASE};
    Flags flags_;
    std::vector<word_t> mem_; // Word-addressed 32-bit memory array (64KB = 16384 words)

    addr_t text_start_{TEXT_BASE};
    addr_t text_end_{TEXT_BASE};

    bool halted_{false};
    bool has_exception_{false};
    std::string exception_msg_;
    PipelineMetrics metrics_;

    // Baseline microprogrammed control unit for comparative benchmarks
    VerticalControlUnit cu_;

    // 4-Stage Latches
    Latch_IF_ID  if_id_;
    Latch_ID_EX  id_ex_;
    Latch_EX_MEM ex_mem_;
    Latch_MEM_WB mem_wb_;

    // 6-Stage Latches
    Latch_IF1_IF2 if1_if2_;
    Latch_IF2_ID  if2_id_;
    Latch_ID_RR   id_rr_;
    Latch_RR_EX   rr_ex_;
    Latch_EX_MEM  ex_mem6_;
    Latch_MEM_WB  mem_wb6_;

    // Hazard & Forwarding Telemetry State
    bool last_stall_{false};
    bool last_flush_{false};
    ForwardSource last_fwd_a_{ForwardSource::NONE};
    ForwardSource last_fwd_b_{ForwardSource::NONE};
};

} // namespace simplerisc

#endif // SIMPLERISC_CPU_PIPELINE_HPP
