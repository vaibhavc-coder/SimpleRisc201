#include <iostream>
#include <iomanip>
#include <cassert>
#include "types.hpp"
#include "alu.hpp"
#include "control_unit.hpp"
#include "cpu_pipeline.hpp"
#include "assembler.hpp"

using namespace simplerisc;

void testALU() {
    std::cout << "\n=======================================================\n";
    std::cout << " 1. TESTING 32-BIT ALU (Smruti Sarangi Architecture)\n";
    std::cout << "=======================================================\n";

    // 1. ADD test
    ALUResult add_res = ALU::execute(ALUOp::ADD, 15, 25);
    std::cout << "[ALU ADD] 15 + 25 = " << add_res.result << " (Expected: 40)\n";

    // 2. SUB test
    ALUResult sub_res = ALU::execute(ALUOp::SUB, 50, 20);
    std::cout << "[ALU SUB] 50 - 20 = " << sub_res.result << " (Expected: 30)\n";

    // 3. MUL test
    ALUResult mul_res = ALU::execute(ALUOp::MUL, 6, 7);
    std::cout << "[ALU MUL] 6 * 7   = " << mul_res.result << " (Expected: 42)\n";

    // 4. CMP test (Equality)
    ALUResult cmp_eq = ALU::execute(ALUOp::CMP, 100, 100);
    std::cout << "[ALU CMP] cmp 100, 100 -> flags.E=" << cmp_eq.flags.E
              << " flags.GT=" << cmp_eq.flags.GT << " (Expected: E=1, GT=0)\n";

    // 5. CMP test (Greater Than)
    ALUResult cmp_gt = ALU::execute(ALUOp::CMP, 200, 50);
    std::cout << "[ALU CMP] cmp 200, 50  -> flags.E=" << cmp_gt.flags.E
              << " flags.GT=" << cmp_gt.flags.GT << " (Expected: E=0, GT=1)\n";
}

void testAssemblerAndControlUnit(const std::string& source_code) {
    std::cout << "\n=======================================================\n";
    std::cout << " 2. TESTING 32-BIT ASSEMBLER (Table 9.2 & 9.3)\n";
    std::cout << "=======================================================\n";

    Assembler assembler;
    AssemblyResult res = assembler.assemble(source_code);

    if (!res.success) {
        std::cerr << "Assembly Error: " << res.error_message << "\n";
        return;
    }

    std::cout << "Assembled " << res.machine_words.size() << " instructions successfully:\n";
    for (size_t i = 0; i < res.machine_words.size(); ++i) {
        std::cout << "  [Word " << i << " @ 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << (i * 4) << "] : 0x" << std::setw(8) << res.machine_words[i] << "\n";
    }

    std::cout << "\n=======================================================\n";
    std::cout << " 3. TESTING VERTICAL MICROPROGRAMMED CONTROL UNIT (Ch. 9)\n";
    std::cout << "=======================================================\n";

    ControlUnit cu;
    cu.loadProgram(res.machine_words, 0);

    std::cout << "Initial Control Unit state:\n";
    std::cout << cu.dumpInternalRegisters();

    std::cout << "\nExecuting instructions through Micro-ROM multi-cycle shared bus...\n";
    uint32_t total_micro_cycles = 0;

    for (size_t inst_idx = 0; inst_idx < res.machine_words.size(); ++inst_idx) {
        uint32_t cycles = cu.stepInstruction();
        total_micro_cycles += cycles;
        std::cout << "  -> Instruction " << std::dec << inst_idx << " finished in " << std::dec << cycles << " micro-cycles.\n";
    }

    std::cout << "\nFinal Register State (r0 - r7):\n";
    for (reg_t r = 0; r < 8; ++r) {
        std::cout << "  r" << static_cast<int>(r) << " = " << std::dec << cu.getRegister(r)
                  << " (0x" << std::hex << cu.getRegister(r) << ")\n";
    }

    std::cout << "\nFlags: [E=" << cu.getFlags().E << " GT=" << cu.getFlags().GT << "]\n";
    std::cout << "Total Micro-Cycles Executed: " << std::dec << total_micro_cycles << "\n";
    std::cout << "Average Micro-CPI: " << std::fixed << std::setprecision(2)
              << (static_cast<double>(total_micro_cycles) / res.machine_words.size()) << "\n";
}

void testPipelinedProcessor() {
    std::cout << "\n=======================================================\n";
    std::cout << " 4. TESTING 4-STAGE PIPELINE (IF -> ID/RR -> EX -> MEM/WB)\n";
    std::cout << "    Features: EX/MEM & MEM/WB Data Forwarding, Stalls, Flushes\n";
    std::cout << "=======================================================\n";

    // Test program designed specifically to trigger:
    // 1. Consecutive RAW data hazards (EX->EX and MEM->EX forwarding)
    // 2. Load-Use hazard (stalling 1 cycle)
    // 3. Taken branch (flushing preceding pipeline stages)
    std::string pipe_code =
        "mov r1, 15\n"
        "mov r2, 25\n"
        "add r3, r1, r2\n"       // RAW Hazard: r3 depends on r1 and r2 (forwarded)
        "sub r4, r3, r1\n"       // RAW Hazard: r4 depends on forwarded r3
        "add r5, r4, 10\n"       // RAW Hazard: r5 depends on forwarded r4
        "st r5, 100(r0)\n"       // Store r5 (35) to address 100
        "ld r6, 100(r0)\n"       // Load memory[100] into r6
        "add r7, r6, 5\n"        // Load-Use Hazard! Must stall 1 cycle! r7 = 40
        "cmp r7, 40\n"
        "beq branch_target\n"    // Taken branch! Flushes pipeline
        "mov r8, 999\n"          // Flushed! Must not execute
        "branch_target:\n"
        "add r9, r7, 10\n"       // r9 = 50
        "nop\n";

    Assembler asm_tool;
    AssemblyResult res = asm_tool.assemble(pipe_code);
    assert(res.success);

    // 4-Stage Pipeline Run
    CPUPipeline pipe4(PipelineMode::FOUR_STAGE);
    pipe4.loadProgram(res.machine_words, 0);

    std::cout << "Running 4-Stage Pipelined Datapath with Telemetry...\n";
    pipe4.run(100);

    std::cout << pipe4.dump4StageState();
    std::cout << pipe4.dumpRegisters();
    std::cout << pipe4.getMetrics().dump();

    // Verify correct execution under pipeline hazards
    assert(pipe4.getRegister(1) == 15);
    assert(pipe4.getRegister(2) == 25);
    assert(pipe4.getRegister(3) == 40);
    assert(pipe4.getRegister(4) == 25);
    assert(pipe4.getRegister(5) == 35);
    assert(pipe4.getRegister(6) == 35);
    assert(pipe4.getRegister(7) == 40);
    assert(pipe4.getRegister(8) == 0); // Flushed! Remained 0
    assert(pipe4.getRegister(9) == 50);
    assert(pipe4.getMetrics().forwarding_events > 0);
    assert(pipe4.getMetrics().stall_cycles > 0);
    assert(pipe4.getMetrics().branch_flushes > 0);
    std::cout << "[PASS] 4-Stage Pipeline Forwarding, Load-Use Stall & Flush Verified!\n";

    // 6-Stage Super-Pipeline Run
    std::cout << "\n=======================================================\n";
    std::cout << " 5. TESTING 6-STAGE SUPER-PIPELINE (IF1->IF2->ID->RR->EX->WB)\n";
    std::cout << "=======================================================\n";

    CPUPipeline pipe6(PipelineMode::SIX_STAGE);
    pipe6.loadProgram(res.machine_words, 0);
    pipe6.run(100);

    std::cout << pipe6.dump6StageState();
    std::cout << pipe6.dumpRegisters();
    std::cout << pipe6.getMetrics().dump();

    assert(pipe6.getRegister(7) == 40);
    assert(pipe6.getRegister(8) == 0); // Flushed
    assert(pipe6.getRegister(9) == 50);
    std::cout << "[PASS] 6-Stage Super-Pipeline Execution Verified!\n";

    // Comparative Performance Table
    std::cout << "\n===============================================================================\n";
    std::cout << " 6. ARCHITECTURAL COMPARATIVE EVALUATION (Microprogrammed vs 4-Stage vs 6-Stage)\n";
    std::cout << "===============================================================================\n";

    // Microprogrammed Baseline Run
    ControlUnit cu;
    cu.loadProgram(res.machine_words, 0);
    uint32_t cu_cycles = 0;
    for (size_t i = 0; i < res.machine_words.size(); ++i) {
        cu_cycles += cu.stepInstruction();
    }
    double cu_cpi = static_cast<double>(cu_cycles) / res.machine_words.size();

    std::cout << std::dec << std::setfill(' ');
    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+\n";
    std::cout << "| Architecture Model    | Clock Cycles  | Retired Inst  | CPI           | Speedup vs CU |\n";
    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+\n";
    std::cout << "| Microprogrammed (CU)  | " << std::setw(13) << cu_cycles
              << " | " << std::setw(13) << res.machine_words.size()
              << " | " << std::fixed << std::setprecision(2) << std::setw(13) << cu_cpi
              << " | " << std::setw(13) << "1.00x" << " |\n";

    double speedup4 = cu_cpi / pipe4.getMetrics().getCPI();
    std::cout << "| 4-Stage Pipeline      | " << std::setw(13) << pipe4.getMetrics().total_cycles
              << " | " << std::setw(13) << pipe4.getMetrics().instructions_retired
              << " | " << std::fixed << std::setprecision(2) << std::setw(13) << pipe4.getMetrics().getCPI()
              << " | " << std::fixed << std::setprecision(2) << std::setw(12) << speedup4 << "x |\n";

    double speedup6 = cu_cpi / pipe6.getMetrics().getCPI();
    std::cout << "| 6-Stage Super-Pipeline| " << std::setw(13) << pipe6.getMetrics().total_cycles
              << " | " << std::setw(13) << pipe6.getMetrics().instructions_retired
              << " | " << std::fixed << std::setprecision(2) << std::setw(13) << pipe6.getMetrics().getCPI()
              << " | " << std::fixed << std::setprecision(2) << std::setw(12) << speedup6 << "x |\n";
    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+\n";
}

int main(int argc, char* argv[]) {
    std::cout << "=======================================================\n";
    std::cout << " SimpleRISC Integrated Processor Simulation Suite\n";
    std::cout << " Features: ALU | Assembler | Micro-CU | 4-Stage & 6-Stage Pipelines\n";
    std::cout << " Reference: Smruti R. Sarangi (Basic Computer Architecture)\n";
    std::cout << "=======================================================\n";

    // 1. ALU Test
    testALU();

    // 2. Assembler & Micro-CU Test
    std::string sample_program =
        "mov r0, 5\n"
        "mov r1, 10\n"
        "mov r2, 20\n"
        "add r3, r1, r2\n"
        "mul r4, r1, r2\n"
        "cmp r1, 10\n"
        "beq is_equal\n"
        "mov r5, 999\n"
        "b finish\n"
        "is_equal:\n"
        "mov r5, 42\n"
        "finish:\n"
        "nop\n";

    testAssemblerAndControlUnit(sample_program);

    // 3. 4-Stage & 6-Stage Pipeline with Forwarding and Stalls
    testPipelinedProcessor();

    // If an external source file was passed, run it through the 4-stage pipeline
    if (argc >= 2) {
        Assembler a;
        AssemblyResult r = a.assembleFile(argv[1]);
        if (r.success) {
            std::cout << "\n=======================================================\n";
            std::cout << " Running File on 4-Stage Pipeline: " << argv[1] << "\n";
            std::cout << "=======================================================\n";
            CPUPipeline file_pipe(PipelineMode::FOUR_STAGE);
            file_pipe.loadProgram(r.machine_words, 0);
            file_pipe.run(50000);
            std::cout << file_pipe.dumpRegisters();
            std::cout << file_pipe.getMetrics().dump();
        }
    }

    return 0;
}
