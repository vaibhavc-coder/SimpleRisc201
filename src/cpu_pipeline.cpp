#include "cpu_pipeline.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace simplerisc {

std::string PipelineMetrics::dump() const {
    std::stringstream ss;
    ss << "+-----------------------------------------------------------------------------+\n";
    ss << "|                     HARDWARE EXECUTION METRICS                              |\n";
    ss << "+-----------------------------------------------------------------------------+\n";
    ss << "| Total Clock Cycles:       " << std::setw(10) << total_cycles << "                                        |\n";
    ss << "| Instructions Retired:     " << std::setw(10) << instructions_retired << "                                        |\n";
    ss << "| Pipeline Stall Cycles:    " << std::setw(10) << stall_cycles << "                                        |\n";
    ss << "| Branch Flush Penalties:   " << std::setw(10) << branch_flushes << "                                        |\n";
    ss << "| Forwarding (Bypass) Hits: " << std::setw(10) << forwarding_events << "                                        |\n";
    ss << "| Load-Use Hazards Trapped: " << std::setw(10) << load_use_hazards << "                                        |\n";
    ss << "| CPI (Cycles / Inst):      " << std::fixed << std::setprecision(3) << std::setw(10) << getCPI() << "                                        |\n";
    ss << "| IPC (Throughput):         " << std::fixed << std::setprecision(3) << std::setw(10) << getIPC() << "                                        |\n";
    ss << "+-----------------------------------------------------------------------------+\n";
    return ss.str();
}

CPUPipeline::CPUPipeline(PipelineMode mode)
    : mode_(mode), mem_(MEMORY_SIZE / 4, 0) {
    reset();
}

void CPUPipeline::reset() {
    regs_.fill(0);
    regs_[SP] = 0x00003FFC; // Default SP top of 16KB
    pc_ = TEXT_BASE;
    flags_ = Flags{};
    halted_ = false;
    has_exception_ = false;
    exception_msg_ = "";
    metrics_ = PipelineMetrics{};

    cu_.reset();

    if_id_ = Latch_IF_ID{};
    id_ex_ = Latch_ID_EX{};
    ex_mem_ = Latch_EX_MEM{};
    mem_wb_ = Latch_MEM_WB{};

    if1_if2_ = Latch_IF1_IF2{};
    if2_id_ = Latch_IF2_ID{};
    id_rr_ = Latch_ID_RR{};
    rr_ex_ = Latch_RR_EX{};
    ex_mem6_ = Latch_EX_MEM{};
    mem_wb6_ = Latch_MEM_WB{};

    last_stall_ = false;
    last_flush_ = false;
    last_fwd_a_ = ForwardSource::NONE;
    last_fwd_b_ = ForwardSource::NONE;
}

void CPUPipeline::loadProgram(const std::vector<word_t>& words, addr_t text_start) {
    reset();
    text_start_ = text_start;
    text_end_ = text_start + static_cast<addr_t>(words.size() * 4);
    pc_ = text_start;

    for (size_t i = 0; i < words.size() && (text_start / 4 + i) < mem_.size(); ++i) {
        mem_[(text_start / 4) + i] = words[i];
    }

    cu_.loadProgram(words, text_start);
}

void CPUPipeline::setPipelineMode(PipelineMode mode) {
    mode_ = mode;
    reset();
}

word_t CPUPipeline::getRegister(reg_t r) const {
    if (r < NUM_REGISTERS) return regs_[r];
    return 0;
}

void CPUPipeline::setRegister(reg_t r, word_t val) {
    if (r < NUM_REGISTERS) regs_[r] = val;
}

word_t CPUPipeline::readMemory(addr_t addr) const {
    size_t word_idx = addr / 4;
    if (word_idx < mem_.size()) return mem_[word_idx];
    return 0;
}

void CPUPipeline::writeMemory(addr_t addr, word_t val) {
    size_t word_idx = addr / 4;
    if (word_idx < mem_.size()) mem_[word_idx] = val;
}

bool CPUPipeline::is4StageEmpty() const {
    return !if_id_.valid && !id_ex_.valid && !ex_mem_.valid && !mem_wb_.valid;
}

bool CPUPipeline::is6StageEmpty() const {
    return !if1_if2_.valid && !if2_id_.valid && !id_rr_.valid && !rr_ex_.valid && !ex_mem6_.valid && !mem_wb6_.valid;
}

// ============================================================================
// 4-STAGE PIPELINE ENGINE (IF -> ID/RR -> EX -> MEM/WB)
// ============================================================================
bool CPUPipeline::step4StageCycle() {
    if (halted_) return false;

    if (pc_ >= text_end_ && is4StageEmpty()) {
        halted_ = true;
        return false;
    }

    metrics_.total_cycles++;
    last_stall_ = false;
    last_flush_ = false;
    last_fwd_a_ = ForwardSource::NONE;
    last_fwd_b_ = ForwardSource::NONE;

    Latch_MEM_WB next_mem_wb{};
    Latch_EX_MEM next_ex_mem{};
    Latch_ID_EX  next_id_ex{};
    Latch_IF_ID  next_if_id{};

    bool stall_pipeline = false;
    bool branch_taken = false;
    addr_t branch_target = 0;

    // ------------------------------------------------------------------------
    // STAGE 4: MEMORY ACCESS & WRITEBACK (MEM/WB)
    // ------------------------------------------------------------------------
    if (ex_mem_.valid && !ex_mem_.bubble) {
        word_t write_data = ex_mem_.alu_result;

        if (ex_mem_.inst.isLoad()) {
            write_data = readMemory(ex_mem_.alu_result);
        } else if (ex_mem_.inst.isStore()) {
            writeMemory(ex_mem_.alu_result, ex_mem_.store_data);
        }

        if (ex_mem_.inst.writesRd()) {
            regs_[ex_mem_.inst.rd] = write_data;
        } else if (ex_mem_.inst.opcode == OP_CALL) {
            regs_[RA] = ex_mem_.pc + 4;
        }

        metrics_.instructions_retired++;

        next_mem_wb.pc = ex_mem_.pc;
        next_mem_wb.inst = ex_mem_.inst;
        next_mem_wb.write_data = write_data;
        next_mem_wb.valid = true;
        next_mem_wb.bubble = false;
    }

    // ------------------------------------------------------------------------
    // STAGE 3: EXECUTE (EX) WITH FORWARDING & BRANCH RESOLUTION
    // ------------------------------------------------------------------------
    if (id_ex_.valid && !id_ex_.bubble) {
        word_t val_a = id_ex_.val_rs1;
        word_t val_b = id_ex_.val_rs2;
        word_t store_data = id_ex_.val_rd;

        // Data Forwarding for Operand A (rs1)
        if (id_ex_.inst.readsRs1()) {
            if (ex_mem_.valid && !ex_mem_.bubble && ex_mem_.inst.writesRd() && (ex_mem_.inst.rd == id_ex_.inst.rs1)) {
                val_a = ex_mem_.inst.isLoad() ? readMemory(ex_mem_.alu_result) : ex_mem_.alu_result;
                last_fwd_a_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (mem_wb_.valid && !mem_wb_.bubble && mem_wb_.inst.writesRd() && (mem_wb_.inst.rd == id_ex_.inst.rs1)) {
                val_a = mem_wb_.write_data;
                last_fwd_a_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        // Data Forwarding for Operand B (rs2)
        if (id_ex_.inst.readsRs2()) {
            if (ex_mem_.valid && !ex_mem_.bubble && ex_mem_.inst.writesRd() && (ex_mem_.inst.rd == id_ex_.inst.rs2)) {
                val_b = ex_mem_.inst.isLoad() ? readMemory(ex_mem_.alu_result) : ex_mem_.alu_result;
                last_fwd_b_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (mem_wb_.valid && !mem_wb_.bubble && mem_wb_.inst.writesRd() && (mem_wb_.inst.rd == id_ex_.inst.rs2)) {
                val_b = mem_wb_.write_data;
                last_fwd_b_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        // Data Forwarding for Store Data (rd)
        if (id_ex_.inst.readsStoreRd()) {
            if (ex_mem_.valid && !ex_mem_.bubble && ex_mem_.inst.writesRd() && (ex_mem_.inst.rd == id_ex_.inst.rd)) {
                store_data = ex_mem_.inst.isLoad() ? readMemory(ex_mem_.alu_result) : ex_mem_.alu_result;
                metrics_.forwarding_events++;
            } else if (mem_wb_.valid && !mem_wb_.bubble && mem_wb_.inst.writesRd() && (mem_wb_.inst.rd == id_ex_.inst.rd)) {
                store_data = mem_wb_.write_data;
                metrics_.forwarding_events++;
            }
        }

        if (id_ex_.inst.I) {
            val_b = id_ex_.inst.immx;
        }

        // ALU Execution
        word_t alu_out = 0;
        if (id_ex_.inst.isLoad() || id_ex_.inst.isStore()) {
            alu_out = val_a + id_ex_.inst.immx;
        } else if (id_ex_.inst.opcode == OP_MOV) {
            alu_out = val_b;
        } else if (id_ex_.inst.opcode == OP_NOT) {
            alu_out = ~val_b;
        } else if (id_ex_.inst.opcode == OP_CMP) {
            auto res = ALU::execute(ALUOp::CMP, val_a, val_b, flags_);
            flags_.E = res.flags.E;
            flags_.GT = res.flags.GT;
            alu_out = res.result;
        } else if (id_ex_.inst.opcode == OP_NOP) {
            alu_out = 0;
        } else {
            ALUOp aop = ALU::fromOpcode(id_ex_.inst.opcode);
            auto res = ALU::execute(aop, val_a, val_b, flags_);
            alu_out = res.result;
        }

        // Branch Resolution
        branch_target = id_ex_.inst.branch_target;
        if (id_ex_.inst.opcode == OP_BEQ) {
            branch_taken = flags_.E;
        } else if (id_ex_.inst.opcode == OP_BGT) {
            branch_taken = flags_.GT;
        } else if (id_ex_.inst.opcode == OP_B || id_ex_.inst.opcode == OP_CALL) {
            branch_taken = true;
        } else if (id_ex_.inst.opcode == OP_RET) {
            branch_taken = true;
            branch_target = regs_[RA];
        }

        if (branch_taken) {
            last_flush_ = true;
            metrics_.branch_flushes++;
            pc_ = branch_target;
        }

        next_ex_mem.pc = id_ex_.pc;
        next_ex_mem.inst = id_ex_.inst;
        next_ex_mem.alu_result = alu_out;
        next_ex_mem.store_data = store_data;
        next_ex_mem.branch_taken = branch_taken;
        next_ex_mem.branch_target = branch_target;
        next_ex_mem.fwd_a = last_fwd_a_;
        next_ex_mem.fwd_b = last_fwd_b_;
        next_ex_mem.valid = true;
        next_ex_mem.bubble = false;
    }

    // ------------------------------------------------------------------------
    // STAGE 2: INSTRUCTION DECODE, REGISTER READ & HAZARD DETECTION (ID/RR)
    // ------------------------------------------------------------------------
    if (branch_taken) {
        next_id_ex.valid = false;
        next_id_ex.bubble = true;
    } else if (if_id_.valid && !if_id_.bubble) {
        Instruction dec = Instruction::decode(if_id_.raw_inst, if_id_.pc);

        // Load-Use Hazard Detection:
        bool load_use_hazard = false;
        if (id_ex_.valid && !id_ex_.bubble && id_ex_.inst.isLoad()) {
            reg_t load_rd = id_ex_.inst.rd;
            if (dec.readsRs1() && (dec.rs1 == load_rd)) load_use_hazard = true;
            if (dec.readsRs2() && (dec.rs2 == load_rd)) load_use_hazard = true;
            if (dec.readsStoreRd() && (dec.rd == load_rd)) load_use_hazard = true;
        }

        if (load_use_hazard) {
            stall_pipeline = true;
            last_stall_ = true;
            metrics_.stall_cycles++;
            metrics_.load_use_hazards++;

            next_id_ex.valid = false;
            next_id_ex.bubble = true;
        } else {
            word_t val_rs1 = regs_[dec.rs1];
            word_t val_rs2 = regs_[dec.rs2];
            word_t val_rd  = regs_[dec.rd];

            // Internal Writeback Forwarding (write-before-read)
            if (next_mem_wb.valid && !next_mem_wb.bubble && next_mem_wb.inst.writesRd()) {
                if (next_mem_wb.inst.rd == dec.rs1) val_rs1 = next_mem_wb.write_data;
                if (next_mem_wb.inst.rd == dec.rs2) val_rs2 = next_mem_wb.write_data;
                if (next_mem_wb.inst.rd == dec.rd)  val_rd  = next_mem_wb.write_data;
            }

            next_id_ex.pc = if_id_.pc;
            next_id_ex.inst = dec;
            next_id_ex.val_rs1 = val_rs1;
            next_id_ex.val_rs2 = val_rs2;
            next_id_ex.val_rd = val_rd;
            next_id_ex.valid = true;
            next_id_ex.bubble = false;
        }
    }

    // ------------------------------------------------------------------------
    // STAGE 1: INSTRUCTION FETCH (IF)
    // ------------------------------------------------------------------------
    if (branch_taken) {
        next_if_id.valid = false;
        next_if_id.bubble = true;
    } else if (stall_pipeline) {
        next_if_id = if_id_;
    } else {
        if (pc_ < text_end_) {
            word_t raw = readMemory(pc_);
            next_if_id.pc = pc_;
            next_if_id.raw_inst = raw;
            next_if_id.valid = true;
            next_if_id.bubble = false;
            pc_ += 4;
        } else {
            next_if_id.valid = false;
            next_if_id.bubble = true;
        }
    }

    mem_wb_ = next_mem_wb;
    ex_mem_ = next_ex_mem;
    id_ex_  = next_id_ex;
    if_id_  = next_if_id;

    return true;
}

// ============================================================================
// 6-STAGE SUPER-PIPELINED ENGINE (IF1 -> IF2 -> ID -> RR -> EX -> WB)
// ============================================================================
bool CPUPipeline::step6StageCycle() {
    if (halted_) return false;

    if (pc_ >= text_end_ && is6StageEmpty()) {
        halted_ = true;
        return false;
    }

    metrics_.total_cycles++;
    last_stall_ = false;
    last_flush_ = false;
    last_fwd_a_ = ForwardSource::NONE;
    last_fwd_b_ = ForwardSource::NONE;

    Latch_MEM_WB  next_mem_wb6{};
    Latch_EX_MEM  next_ex_mem6{};
    Latch_RR_EX   next_rr_ex{};
    Latch_ID_RR   next_id_rr{};
    Latch_IF2_ID  next_if2_id{};
    Latch_IF1_IF2 next_if1_if2{};

    bool stall_6stage = false;
    bool branch_taken = false;
    addr_t branch_target = 0;

    // STAGE 6: WB
    if (ex_mem6_.valid && !ex_mem6_.bubble) {
        word_t write_data = ex_mem6_.alu_result;
        if (ex_mem6_.inst.isLoad()) {
            write_data = readMemory(ex_mem6_.alu_result);
        } else if (ex_mem6_.inst.isStore()) {
            writeMemory(ex_mem6_.alu_result, ex_mem6_.store_data);
        }

        if (ex_mem6_.inst.writesRd()) {
            regs_[ex_mem6_.inst.rd] = write_data;
        } else if (ex_mem6_.inst.opcode == OP_CALL) {
            regs_[RA] = ex_mem6_.pc + 4;
        }

        metrics_.instructions_retired++;

        next_mem_wb6.pc = ex_mem6_.pc;
        next_mem_wb6.inst = ex_mem6_.inst;
        next_mem_wb6.write_data = write_data;
        next_mem_wb6.valid = true;
        next_mem_wb6.bubble = false;
    }

    // STAGE 5: EX
    if (rr_ex_.valid && !rr_ex_.bubble) {
        word_t val_a = rr_ex_.val_rs1;
        word_t val_b = rr_ex_.val_rs2;
        word_t store_data = rr_ex_.val_rd;

        if (rr_ex_.inst.readsRs1()) {
            if (ex_mem6_.valid && !ex_mem6_.bubble && ex_mem6_.inst.writesRd() && (ex_mem6_.inst.rd == rr_ex_.inst.rs1)) {
                val_a = ex_mem6_.inst.isLoad() ? readMemory(ex_mem6_.alu_result) : ex_mem6_.alu_result;
                last_fwd_a_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (mem_wb6_.valid && !mem_wb6_.bubble && mem_wb6_.inst.writesRd() && (mem_wb6_.inst.rd == rr_ex_.inst.rs1)) {
                val_a = mem_wb6_.write_data;
                last_fwd_a_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        if (rr_ex_.inst.readsRs2()) {
            if (ex_mem6_.valid && !ex_mem6_.bubble && ex_mem6_.inst.writesRd() && (ex_mem6_.inst.rd == rr_ex_.inst.rs2)) {
                val_b = ex_mem6_.inst.isLoad() ? readMemory(ex_mem6_.alu_result) : ex_mem6_.alu_result;
                last_fwd_b_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (mem_wb6_.valid && !mem_wb6_.bubble && mem_wb6_.inst.writesRd() && (mem_wb6_.inst.rd == rr_ex_.inst.rs2)) {
                val_b = mem_wb6_.write_data;
                last_fwd_b_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        if (rr_ex_.inst.readsStoreRd()) {
            if (ex_mem6_.valid && !ex_mem6_.bubble && ex_mem6_.inst.writesRd() && (ex_mem6_.inst.rd == rr_ex_.inst.rd)) {
                store_data = ex_mem6_.inst.isLoad() ? readMemory(ex_mem6_.alu_result) : ex_mem6_.alu_result;
                metrics_.forwarding_events++;
            } else if (mem_wb6_.valid && !mem_wb6_.bubble && mem_wb6_.inst.writesRd() && (mem_wb6_.inst.rd == rr_ex_.inst.rd)) {
                store_data = mem_wb6_.write_data;
                metrics_.forwarding_events++;
            }
        }

        if (rr_ex_.inst.I) val_b = rr_ex_.inst.immx;

        word_t alu_out = 0;
        if (rr_ex_.inst.isLoad() || rr_ex_.inst.isStore()) {
            alu_out = val_a + rr_ex_.inst.immx;
        } else if (rr_ex_.inst.opcode == OP_MOV) {
            alu_out = val_b;
        } else if (rr_ex_.inst.opcode == OP_NOT) {
            alu_out = ~val_b;
        } else if (rr_ex_.inst.opcode == OP_CMP) {
            auto res = ALU::execute(ALUOp::CMP, val_a, val_b, flags_);
            flags_.E = res.flags.E;
            flags_.GT = res.flags.GT;
            alu_out = res.result;
        } else if (rr_ex_.inst.opcode == OP_NOP) {
            alu_out = 0;
        } else {
            ALUOp aop = ALU::fromOpcode(rr_ex_.inst.opcode);
            auto res = ALU::execute(aop, val_a, val_b, flags_);
            alu_out = res.result;
        }

        branch_target = rr_ex_.inst.branch_target;
        if (rr_ex_.inst.opcode == OP_BEQ) branch_taken = flags_.E;
        else if (rr_ex_.inst.opcode == OP_BGT) branch_taken = flags_.GT;
        else if (rr_ex_.inst.opcode == OP_B || rr_ex_.inst.opcode == OP_CALL) branch_taken = true;
        else if (rr_ex_.inst.opcode == OP_RET) {
            branch_taken = true;
            branch_target = regs_[RA];
        }

        if (branch_taken) {
            last_flush_ = true;
            metrics_.branch_flushes++;
            pc_ = branch_target;
        }

        next_ex_mem6.pc = rr_ex_.pc;
        next_ex_mem6.inst = rr_ex_.inst;
        next_ex_mem6.alu_result = alu_out;
        next_ex_mem6.store_data = store_data;
        next_ex_mem6.branch_taken = branch_taken;
        next_ex_mem6.branch_target = branch_target;
        next_ex_mem6.fwd_a = last_fwd_a_;
        next_ex_mem6.fwd_b = last_fwd_b_;
        next_ex_mem6.valid = true;
        next_ex_mem6.bubble = false;
    }

    // STAGE 4: RR
    if (branch_taken) {
        next_rr_ex.valid = false;
        next_rr_ex.bubble = true;
    } else if (id_rr_.valid && !id_rr_.bubble) {
        word_t val_rs1 = regs_[id_rr_.inst.rs1];
        word_t val_rs2 = regs_[id_rr_.inst.rs2];
        word_t val_rd  = regs_[id_rr_.inst.rd];

        if (next_mem_wb6.valid && !next_mem_wb6.bubble && next_mem_wb6.inst.writesRd()) {
            if (next_mem_wb6.inst.rd == id_rr_.inst.rs1) val_rs1 = next_mem_wb6.write_data;
            if (next_mem_wb6.inst.rd == id_rr_.inst.rs2) val_rs2 = next_mem_wb6.write_data;
            if (next_mem_wb6.inst.rd == id_rr_.inst.rd)  val_rd  = next_mem_wb6.write_data;
        }

        next_rr_ex.pc = id_rr_.pc;
        next_rr_ex.inst = id_rr_.inst;
        next_rr_ex.val_rs1 = val_rs1;
        next_rr_ex.val_rs2 = val_rs2;
        next_rr_ex.val_rd = val_rd;
        next_rr_ex.valid = true;
        next_rr_ex.bubble = false;
    }

    // STAGE 3: ID
    if (branch_taken) {
        next_id_rr.valid = false;
        next_id_rr.bubble = true;
    } else if (if2_id_.valid && !if2_id_.bubble) {
        Instruction dec = Instruction::decode(if2_id_.raw_inst, if2_id_.pc);

        bool load_use = false;
        if (rr_ex_.valid && !rr_ex_.bubble && rr_ex_.inst.isLoad()) {
            reg_t lrd = rr_ex_.inst.rd;
            if ((dec.readsRs1() && dec.rs1 == lrd) ||
                (dec.readsRs2() && dec.rs2 == lrd) ||
                (dec.readsStoreRd() && dec.rd == lrd)) {
                load_use = true;
            }
        }

        if (load_use) {
            stall_6stage = true;
            last_stall_ = true;
            metrics_.stall_cycles++;
            metrics_.load_use_hazards++;

            next_id_rr.valid = false;
            next_id_rr.bubble = true;
        } else {
            next_id_rr.pc = if2_id_.pc;
            next_id_rr.inst = dec;
            next_id_rr.valid = true;
            next_id_rr.bubble = false;
        }
    }

    // STAGE 2: IF2
    if (branch_taken) {
        next_if2_id.valid = false;
        next_if2_id.bubble = true;
    } else if (stall_6stage) {
        next_if2_id = if2_id_;
    } else if (if1_if2_.valid && !if1_if2_.bubble) {
        next_if2_id.pc = if1_if2_.pc;
        next_if2_id.raw_inst = readMemory(if1_if2_.pc);
        next_if2_id.valid = true;
        next_if2_id.bubble = false;
    }

    // STAGE 1: IF1
    if (branch_taken) {
        next_if1_if2.valid = false;
        next_if1_if2.bubble = true;
    } else if (stall_6stage) {
        next_if1_if2 = if1_if2_;
    } else {
        if (pc_ < text_end_) {
            next_if1_if2.pc = pc_;
            next_if1_if2.valid = true;
            next_if1_if2.bubble = false;
            pc_ += 4;
        } else {
            next_if1_if2.valid = false;
            next_if1_if2.bubble = true;
        }
    }

    mem_wb6_ = next_mem_wb6;
    ex_mem6_ = next_ex_mem6;
    rr_ex_   = next_rr_ex;
    id_rr_   = next_id_rr;
    if2_id_  = next_if2_id;
    if1_if2_ = next_if1_if2;

    return true;
}

// ============================================================================
// MICROPROGRAMMED SHARED-BUS ENGINE (Sarangi Ch. 9)
// ============================================================================
bool CPUPipeline::stepMicroCycle() {
    if (halted_) return false;

    if (cu_.getPC() >= text_end_ && cu_.getUPC() == 0) {
        halted_ = true;
        return false;
    }

    metrics_.total_cycles++;
    bool done = cu_.stepMicroCycle();
    if (done) metrics_.instructions_retired++;

    // Mirror registers from control unit to pipeline
    for (reg_t i = 0; i < NUM_REGISTERS; ++i) {
        regs_[i] = cu_.getRegister(i);
    }
    pc_ = cu_.getPC();
    flags_ = cu_.getFlags();

    return true;
}

// ============================================================================
// HIGH-LEVEL STEPPING AND RUNNER
// ============================================================================
bool CPUPipeline::cycleStep() {
    switch (mode_) {
        case PipelineMode::FOUR_STAGE:
            return step4StageCycle();
        case PipelineMode::SIX_STAGE:
            return step6StageCycle();
        case PipelineMode::MICROPROGRAMMED:
            return stepMicroCycle();
    }
    return false;
}

bool CPUPipeline::macroStep() {
    if (halted_) return false;

    if (mode_ == PipelineMode::MICROPROGRAMMED) {
        uint32_t micro_cycles = cu_.stepInstruction();
        metrics_.total_cycles += micro_cycles;
        metrics_.instructions_retired++;
        for (reg_t i = 0; i < NUM_REGISTERS; ++i) {
            regs_[i] = cu_.getRegister(i);
        }
        pc_ = cu_.getPC();
        flags_ = cu_.getFlags();
        if (pc_ >= text_end_) halted_ = true;
        return true;
    } else {
        uint64_t prev = metrics_.instructions_retired;
        while (!halted_) {
            if (!cycleStep()) break;
            if (metrics_.instructions_retired > prev) return true;
        }
        return !halted_;
    }
}

void CPUPipeline::run(uint64_t max_cycles) {
    while (!halted_ && metrics_.total_cycles < max_cycles) {
        if (!cycleStep()) break;
    }
}

// ============================================================================
// VISUALIZERS
// ============================================================================
static std::string fwdStr(ForwardSource f) {
    switch (f) {
        case ForwardSource::EX_MEM: return "EX/MEM";
        case ForwardSource::MEM_WB: return "MEM/WB";
        default: return "NONE";
    }
}

std::string CPUPipeline::dump4StageState() const {
    std::stringstream ss;
    ss << "+-----------------------------------------------------------------------------+\n";
    ss << "|                   4-STAGE PIPELINE HARDWARE OCCUPANCY                       |\n";
    ss << "+-----------------------------------------------------------------------------+\n";

    auto formatStage = [](const std::string& name, addr_t pc, word_t raw, bool valid, const std::string& extra = "") {
        std::stringstream line;
        line << "| [" << std::setw(6) << std::left << name << "] ";
        if (!valid) {
            line << "BUBBLE / EMPTY" << std::setw(47) << " " << "|\n";
        } else {
            line << "PC: " << toHex8(pc) << " | Raw: " << toHex8(raw) << " " << std::setw(37) << " " << "|\n";
            if (!extra.empty()) {
                line << "|          " << std::setw(65) << std::left << extra << " |\n";
            }
        }
        return line.str();
    };

    ss << formatStage("IF", if_id_.pc, if_id_.raw_inst, if_id_.valid);
    ss << formatStage("ID/RR", id_ex_.pc, id_ex_.inst.raw, id_ex_.valid,
                      "Decoded Rd: r" + std::to_string(id_ex_.inst.rd) + ", Rs1: r" + std::to_string(id_ex_.inst.rs1));
    ss << formatStage("EX", ex_mem_.pc, ex_mem_.inst.raw, ex_mem_.valid,
                      "ALU Result: " + toHex8(ex_mem_.alu_result) + " | Fwd A: " + fwdStr(ex_mem_.fwd_a) + ", B: " + fwdStr(ex_mem_.fwd_b));
    ss << formatStage("MEM/WB", mem_wb_.pc, mem_wb_.inst.raw, mem_wb_.valid,
                      "Writeback Val: " + toHex8(mem_wb_.write_data));

    ss << "+-----------------------------------------------------------------------------+\n";
    ss << "| Telemetry: Stall: " << (last_stall_ ? "YES" : "NO ")
       << " | Flush: " << (last_flush_ ? "YES" : "NO ")
       << " | CPI: " << std::fixed << std::setprecision(3) << metrics_.getCPI()
       << " | IPC: " << std::fixed << std::setprecision(3) << metrics_.getIPC()
       << "                               |\n";
    ss << "+-----------------------------------------------------------------------------+\n";
    return ss.str();
}

std::string CPUPipeline::dump6StageState() const {
    std::stringstream ss;
    ss << "+-----------------------------------------------------------------------------+\n";
    ss << "|                6-STAGE SUPER-PIPELINE HARDWARE OCCUPANCY                    |\n";
    ss << "+-----------------------------------------------------------------------------+\n";

    auto formatStage = [](const std::string& name, addr_t pc, word_t raw, bool valid, const std::string& extra = "") {
        std::stringstream line;
        line << "| [" << std::setw(5) << std::left << name << "] ";
        if (!valid) {
            line << "BUBBLE / EMPTY" << std::setw(48) << " " << "|\n";
        } else {
            line << "PC: " << toHex8(pc) << " | Raw: " << toHex8(raw) << " " << std::setw(38) << " " << "|\n";
            if (!extra.empty()) {
                line << "|         " << std::setw(66) << std::left << extra << " |\n";
            }
        }
        return line.str();
    };

    ss << formatStage("IF1", if1_if2_.pc, 0, if1_if2_.valid, "Address Gen: " + toHex8(if1_if2_.pc));
    ss << formatStage("IF2", if2_id_.pc, if2_id_.raw_inst, if2_id_.valid, "Memory Latch");
    ss << formatStage("ID", id_rr_.pc, id_rr_.inst.raw, id_rr_.valid, "Instruction Decode & Hazard Check");
    ss << formatStage("RR", rr_ex_.pc, rr_ex_.inst.raw, rr_ex_.valid, "Register Read & Forward Setup");
    ss << formatStage("EX", ex_mem6_.pc, ex_mem6_.inst.raw, ex_mem6_.valid, "ALU Result: " + toHex8(ex_mem6_.alu_result));
    ss << formatStage("WB", mem_wb6_.pc, mem_wb6_.inst.raw, mem_wb6_.valid, "Committed to Register File: " + toHex8(mem_wb6_.write_data));

    ss << "+-----------------------------------------------------------------------------+\n";
    ss << "| Telemetry: Stall: " << (last_stall_ ? "YES" : "NO ")
       << " | Flush: " << (last_flush_ ? "YES" : "NO ")
       << " | CPI: " << std::fixed << std::setprecision(3) << metrics_.getCPI()
       << " | IPC: " << std::fixed << std::setprecision(3) << metrics_.getIPC()
       << "                               |\n";
    ss << "+-----------------------------------------------------------------------------+\n";
    return ss.str();
}

std::string CPUPipeline::dumpPipelineState() const {
    if (mode_ == PipelineMode::FOUR_STAGE) {
        return dump4StageState();
    } else if (mode_ == PipelineMode::SIX_STAGE) {
        return dump6StageState();
    } else {
        std::stringstream ss;
        ss << cu_.dumpInternalRegisters();
        return ss.str();
    }
}

std::string CPUPipeline::dumpRegisters() const {
    std::stringstream ss;
    ss << "=== REGISTER FILE (r0 - r15) ===\n";
    for (size_t i = 0; i < NUM_REGISTERS; ++i) {
        ss << "  " << std::setw(4) << std::left << regName(static_cast<reg_t>(i)) << " = "
           << std::setw(10) << regs_[i] << " (" << toHex8(regs_[i]) << ")\n";
    }
    ss << "PC : " << toHex8(pc_) << " | Flags: [E=" << flags_.E << " GT=" << flags_.GT << "]\n";
    return ss.str();
}

} // namespace simplerisc
