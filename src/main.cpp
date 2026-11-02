#include <iostream>
#include <iomanip>
#include <cassert>
#include <string>
#include <cmath>
#include <cstring>
#include "types.hpp"
#include "alu.hpp"
#include "vertical_control_unit.hpp"
#include "horizontal_control_unit.hpp"
#include "cpu_pipeline.hpp"
#include "assembler.hpp"
#include "disassembler.hpp"
#include "stack_preprocessor.hpp"
#include "exception_framework.hpp"
#include "stack_visualizer.hpp"
#include "debugger_cli.hpp"

using namespace simplerisc;

void testALU() {
    std::cout << "\n===============================================================================\n";
    std::cout << " 1. TESTING 32-BIT HARDWARE-OPTIMIZED ALU (Radix-4 Booth, CLA, Newton-Raphson)\n";
    std::cout << "===============================================================================\n";

    // 1. Hierarchical GPK CLA Adder & Subtractor
    ALUResult add_res = ALU::execute(ALUOp::ADD, 15, 25);
    std::cout << "[CLA ADD] 15 + 25 = " << add_res.result << " | Flags: C=" << add_res.carry() << " V=" << add_res.overflow() << " (Expected: 40)\n";
    assert(add_res.result == 40 && !add_res.carry() && !add_res.overflow());

    ALUResult add_carry = ALU::execute(ALUOp::ADD, 0xFFFFFFFFU, 1);
    std::cout << "[CLA CARRY] 0xFFFFFFFF + 1 = 0x" << std::hex << add_carry.result << std::dec << " | Flags: C=" << add_carry.carry() << " (Expected: C=1)\n";
    assert(add_carry.carry());

    ALUResult sub_res = ALU::execute(ALUOp::SUB, 50, 20);
    std::cout << "[CLA SUB] 50 - 20 = " << sub_res.result << " | Flags: C=" << sub_res.carry() << " (Expected: 30, no borrow C=1)\n";
    assert(sub_res.result == 30 && sub_res.carry());

    // 2. Radix-4 Modified Booth Multiplier (16 iterations, 3-bit window recoding)
    ALUResult mul_res = ALU::execute(ALUOp::MUL, 6, 7);
    std::cout << "[BOOTH MUL] 6 * 7   = " << mul_res.result << " (Expected: 42)\n";
    assert(mul_res.result == 42);

    ALUResult mul_neg = ALU::execute(ALUOp::MUL, 50, static_cast<word_t>(-4));
    std::cout << "[BOOTH MUL] 50 * -4 = " << static_cast<sword_t>(mul_neg.result) << " | Flags: N=" << mul_neg.negative() << " (Expected: -200)\n";
    assert(static_cast<sword_t>(mul_neg.result) == -200 && mul_neg.negative());

    // 3. Newton-Raphson Integer Divider & Modulo
    ALUResult div_res = ALU::execute(ALUOp::DIV, 355, 113);
    ALUResult mod_res = ALU::execute(ALUOp::MOD, 355, 113);
    std::cout << "[NEWTON-RAPHSON] 355 / 113 = " << div_res.result << ", 355 % 113 = " << mod_res.result << " (Expected: Q=3, R=16)\n";
    assert(div_res.result == 3 && mod_res.result == 16);
    assert(113 * div_res.result + mod_res.result == 355);

    // 4. Comparison Flags (E, GT, C, V, N, Z)
    ALUResult cmp_eq = ALU::execute(ALUOp::CMP, 100, 100);
    std::cout << "[ALU CMP] cmp 100, 100 -> flags.E=" << cmp_eq.flags.E
              << " flags.GT=" << cmp_eq.flags.GT << " (Expected: E=1, GT=0)\n";
    assert(cmp_eq.flags.E && !cmp_eq.flags.GT);

    ALUResult cmp_gt = ALU::execute(ALUOp::CMP, 200, 50);
    std::cout << "[ALU CMP] cmp 200, 50  -> flags.E=" << cmp_gt.flags.E
              << " flags.GT=" << cmp_gt.flags.GT << " (Expected: E=0, GT=1)\n";
    assert(!cmp_gt.flags.E && cmp_gt.flags.GT);

    // 5. Bit-Level Conversions (I2D, D2I, CLZ)
    sword_t test_val = -12345;
    uint64_t dbits = ALU::intToDoubleBits(test_val);
    sword_t recovered = ALU::doubleBitsToInt(dbits);
    std::cout << "[I2D / D2I] Value: " << test_val << " -> Double bit pattern -> Recovered: " << recovered << " (Expected: -12345)\n";
    assert(recovered == test_val);

    // 6. IEEE-754 Single-Precision Floating-Point (Fig 8.18 FMUL & Newton-Raphson FDIV)
    float fa = 3.5f, fb = 2.0f;
    word_t wa, wb;
    std::memcpy(&wa, &fa, sizeof(float));
    std::memcpy(&wb, &fb, sizeof(float));
    ALUResult fmul_res = ALU::execute(ALUOp::FMUL, wa, wb);
    float f_res;
    std::memcpy(&f_res, &fmul_res.result, sizeof(float));
    std::cout << "[IEEE-754 FMUL] 3.5 * 2.0 = " << f_res << " (Expected: 7.0)\n";
    assert(std::fabs(f_res - 7.0f) < 1e-6f);

    bool fdiv_ovf = false, fdiv_unf = false, fdiv_div0 = false;
    word_t fdiv_w = ALU::fpDivide32(wa, wb, fdiv_ovf, fdiv_unf, fdiv_div0);
    float fdiv_val;
    std::memcpy(&fdiv_val, &fdiv_w, sizeof(float));
    std::cout << "[IEEE-754 FDIV] 3.5 / 2.0 = " << fdiv_val << " (Expected: 1.75)\n";
    assert(std::fabs(fdiv_val - 1.75f) < 1e-6f);
    std::cout << "[PASS] All 32-Bit ALU Hardware Optimizations & Arithmetic Tests Passed!\n";
}

void testStackPreprocessorAndDisassembler() {
    std::cout << "\n=======================================================\n";
    std::cout << " 2. TESTING STACK PREPROCESSOR & MACRO EXPANSION\n";
    std::cout << "=======================================================\n";

    std::string macro_code =
        "main:\n"
        "  mov r1, 10\n"
        "  mov r2, 20\n"
        "  push r1\n"
        "  push r2\n"
        "  enter 8\n"
        "  leave 8\n"
        "  pop r2\n"
        "  pop r1\n"
        "  nop\n";

    StackPreprocessor prep;
    PreprocessorReport prep_rep = prep.process(macro_code);
    std::cout << "Original lines: " << prep_rep.original_lines
              << " | Expanded lines: " << prep_rep.generated_lines
              << " | Macros expanded: " << prep_rep.macros_expanded << "\n";
    std::cout << "Expanded Assembly:\n" << prep_rep.expanded_source << "\n";

    std::cout << "\n=======================================================\n";
    std::cout << " 3. TESTING TWO-PASS ASSEMBLER & DISASSEMBLER\n";
    std::cout << "=======================================================\n";

    Assembler asm_tool;
    AssemblyResult asm_res = asm_tool.assemble(prep_rep.expanded_source);
    assert(asm_res.success);

    std::cout << "Assembled " << asm_res.machine_words.size() << " machine words.\n";
    auto disasm_list = Disassembler::disassembleProgram(asm_res.machine_words, 0);
    std::cout << Disassembler::formatListing(disasm_list);
}

void testStackVisualizerAndExceptions() {
    std::cout << "\n=======================================================\n";
    std::cout << " 4. TESTING ACTIVE ASCII STACK VISUALIZER & EXCEPTION FRAMEWORK\n";
    std::cout << "=======================================================\n";

    std::vector<word_t> test_mem(4096, 0);
    addr_t sp = 0x00003FF0;
    test_mem[0x00003FFC / 4] = 0xCAFEBABE; // Initial base value
    test_mem[0x00003FF8 / 4] = 100;
    test_mem[0x00003FF4 / 4] = 200;
    test_mem[0x00003FF0 / 4] = 300;

    std::cout << StackVisualizer::renderWithContext(test_mem, sp, 0x14, 0x100, 0x00003FFC, 0x00002000, 6);

    // Test Exception Verification
    ExceptionFramework ex_guard(StackBounds{0x00003FFC, 0x00002000, true});
    std::cout << "Testing Stack Overflow Guard (SP = 0x00001000):\n";
    ex_guard.checkStackPointer(0x00001000, 0x20, 45);
    std::cout << ex_guard.dumpExceptionReport();

    std::cout << "Testing Division-by-Zero Guard (divisor = 0):\n";
    ex_guard.checkDivision(0, 0x28, 48);
    std::cout << ex_guard.dumpExceptionReport();
}

void testMicroprogrammedControllers() {
    std::cout << "\n=======================================================\n";
    std::cout << " 5. MICROPROGRAMMED CONTROLLERS: VERTICAL vs HORIZONTAL\n";
    std::cout << "=======================================================\n";

    std::string sample =
        "mov r1, 10\n"
        "mov r2, 20\n"
        "add r3, r1, r2\n"
        "cmp r3, 30\n"
        "beq target\n"
        "mov r4, 1\n"
        "target:\n"
        "mov r4, 99\n"
        "nop\n";

    Assembler a;
    AssemblyResult res = a.assemble(sample);
    assert(res.success);

    // 1. Vertical Microprogrammed Control Unit
    VerticalControlUnit vertical_cu;
    vertical_cu.loadProgram(res.machine_words, 0);
    uint32_t vert_cycles = 0;
    for (size_t i = 0; i < res.machine_words.size(); ++i) {
        vert_cycles += vertical_cu.stepInstruction();
    }
    double vert_cpi = static_cast<double>(vert_cycles) / res.machine_words.size();

    // 2. Horizontal Microprogrammed Control Unit
    HorizontalControlUnit horiz_cu;
    horiz_cu.loadProgram(res.machine_words, 0);
    uint32_t horiz_cycles = 0;
    for (size_t i = 0; i < res.machine_words.size(); ++i) {
        horiz_cycles += horiz_cu.stepInstruction();
    }
    double horiz_cpi = static_cast<double>(horiz_cycles) / res.machine_words.size();

    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+\n";
    std::cout << "| Controller Model      | Word Width    | Decoder Delay | Total uCycles | Micro-CPI     |\n";
    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+\n";
    std::cout << "| Vertical (Ch. 9)      | 45 bits       | Gate Decode   | "
              << std::setw(13) << vert_cycles << " | "
              << std::fixed << std::setprecision(2) << std::setw(13) << vert_cpi << " |\n";
    std::cout << "| Horizontal            | 64 bits       | 0 ns (Direct) | "
              << std::setw(13) << horiz_cycles << " | "
              << std::fixed << std::setprecision(2) << std::setw(13) << horiz_cpi << " |\n";
    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+\n";
}

void testPipelinedProcessor() {
    std::cout << "\n=======================================================\n";
    std::cout << " 6. TESTING 4-STAGE & 6-STAGE PIPELINES WITH HAZARDS\n";
    std::cout << "=======================================================\n";

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

    CPUPipeline pipe4(PipelineMode::FOUR_STAGE);
    pipe4.loadProgram(res.machine_words, 0);
    pipe4.run(100);

    std::cout << pipe4.dump4StageState();
    std::cout << pipe4.dumpRegisters();
    std::cout << pipe4.getMetrics().dump();

    assert(pipe4.getRegister(1) == 15);
    assert(pipe4.getRegister(2) == 25);
    assert(pipe4.getRegister(3) == 40);
    assert(pipe4.getRegister(4) == 25);
    assert(pipe4.getRegister(5) == 35);
    assert(pipe4.getRegister(6) == 35);
    assert(pipe4.getRegister(7) == 40);
    assert(pipe4.getRegister(8) == 0); // Flushed
    assert(pipe4.getRegister(9) == 50);
    std::cout << "[PASS] 4-Stage Pipeline Forwarding, Load-Use Stall & Flush Verified!\n";

    CPUPipeline pipe6(PipelineMode::SIX_STAGE);
    pipe6.loadProgram(res.machine_words, 0);
    pipe6.run(100);

    std::cout << pipe6.dump6StageState();
    std::cout << pipe6.dumpRegisters();
    std::cout << pipe6.getMetrics().dump();

    assert(pipe6.getRegister(1) == 15);
    assert(pipe6.getRegister(2) == 25);
    assert(pipe6.getRegister(3) == 40);
    assert(pipe6.getRegister(4) == 25);
    assert(pipe6.getRegister(5) == 35);
    assert(pipe6.getRegister(6) == 35);
    assert(pipe6.getRegister(7) == 40);
    assert(pipe6.getRegister(8) == 0);
    assert(pipe6.getRegister(9) == 50);
    std::cout << "[PASS] 6-Stage Super-Pipeline Execution Verified!\n";

    std::cout << "\n===============================================================================\n";
    std::cout << " 7. ARCHITECTURAL COMPARATIVE EVALUATION\n";
    std::cout << "===============================================================================\n";

    VerticalControlUnit cu;
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

void testDatapathOptimizations() {
    std::cout << "\n=======================================================\n";
    std::cout << " 6.B TESTING ADVANCED DATAPATH (RAS, STORE-FORWARD, L1 CACHES)\n";
    std::cout << "=======================================================\n";

    std::string test_code = 
        "mov r1, 100\n"
        "st r1, 200(r0)\n"        // Store r1 to addr 200
        "ld r2, 200(r0)\n"        // Immediate load from addr 200 (Store-to-Load Forwarding!)
        "call my_sub\n"           // Call subroutine (Pushes PC+4 to Return Address Stack)
        "mov r4, 77\n"
        "b end_test\n"
        "my_sub:\n"
        "add r3, r2, 50\n"        // r3 = 100 + 50 = 150
        "ret\n"                   // Return (RAS speculative predict target = mov r4, 77)
        "end_test:\n"
        "nop\n";

    Assembler asm_tool;
    auto res = asm_tool.assemble(test_code);
    assert(res.success);

    // Verify on 4-Stage Pipeline
    CPUPipeline pipe4(PipelineMode::FOUR_STAGE);
    pipe4.loadProgram(res.machine_words, 0);
    pipe4.run(50);

    assert(pipe4.getRegister(1) == 100);
    assert(pipe4.getRegister(2) == 100);
    assert(pipe4.getRegister(3) == 150);
    assert(pipe4.getRegister(4) == 77);
    assert(pipe4.getMetrics().store_load_forwards >= 1);
    assert(pipe4.getMetrics().ras_hits >= 1);

    // Verify on 6-Stage Super-Pipeline
    CPUPipeline pipe6(PipelineMode::SIX_STAGE);
    pipe6.loadProgram(res.machine_words, 0);
    pipe6.run(50);

    assert(pipe6.getRegister(1) == 100);
    assert(pipe6.getRegister(2) == 100);
    assert(pipe6.getRegister(3) == 150);
    assert(pipe6.getRegister(4) == 77);
    assert(pipe6.getMetrics().store_load_forwards >= 1);
    assert(pipe6.getMetrics().ras_hits >= 1);

    std::cout << pipe4.dumpRegisters();
    std::cout << pipe4.getMetrics().dump();
    std::cout << "[PASS] Advanced Datapath Optimizations Verified on both 4-Stage & 6-Stage Pipelines!\n";
}

int main(int argc, char* argv[]) {
    std::string mode_str = "both";
    std::string file_path = "";
    bool run_tests = true;
    bool interactive = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./simplerisc_sim.exe [options] [file.s]\n\n"
                      << "Options:\n"
                      << "  --cli, -i, --debugger       Launch interactive CLI debugger\n"
                      << "  --mode, -m <4|6|both>       Set pipeline execution mode (default: both)\n"
                      << "  --no-tests                  Skip verification test suites and run file/CLI directly\n"
                      << "  --help, -h                  Display this help message\n\n"
                      << "Sample Programs:\n"
                      << "  ./simplerisc_sim.exe programs/demo.s\n"
                      << "  ./simplerisc_sim.exe --mode 6 programs/test_hazards.s\n"
                      << "  ./simplerisc_sim.exe --cli programs/test_stack_macros.s\n";
            return 0;
        } else if (arg == "--cli" || arg == "-i" || arg == "--debugger") {
            interactive = true;
        } else if (arg == "--no-tests") {
            run_tests = false;
        } else if (arg == "--mode" || arg == "-m") {
            if (i + 1 < argc) {
                mode_str = argv[++i];
            }
        } else if (arg[0] != '-') {
            file_path = arg;
        }
    }

    if (interactive) {
        PipelineMode pmode = PipelineMode::FOUR_STAGE;
        if (mode_str == "6" || mode_str == "6stage") pmode = PipelineMode::SIX_STAGE;
        CPUPipeline pipeline(pmode);
        if (!file_path.empty()) {
            StackPreprocessor prep;
            PreprocessorReport pr = prep.processFile(file_path);
            if (pr.success) {
                Assembler a;
                AssemblyResult r = a.assemble(pr.expanded_source);
                if (r.success) {
                    pipeline.loadProgram(r.machine_words, 0);
                    std::cout << "Loaded program " << file_path << " (" << r.machine_words.size() << " instructions, " << pr.macros_expanded << " macros expanded)\n";
                }
            }
        }
        DebuggerCLI dbg(pipeline);
        dbg.runInteractive();
        return 0;
    }

    if (run_tests) {
        std::cout << "===============================================================================\n";
        std::cout << "   RISC201 COMPLETE INTEGRATED PROCESSOR SIMULATION & EVALUATION SUITE        \n";
        std::cout << "   Computer Organization & Architecture (CSC-201) — Autumn 2026               \n";
        std::cout << "   Reference: Smruti R. Sarangi (Basic Computer Architecture, Chapter 9 & 10)  \n";
        std::cout << "===============================================================================\n";

        // 1. ALU verification
        testALU();

        // 2. Stack Preprocessor & Disassembler
        testStackPreprocessorAndDisassembler();

        // 3. Stack Visualizer & Runtime Exception Framework
        testStackVisualizerAndExceptions();

        // 4. Horizontal vs Vertical Microprogramming Comparison
        testMicroprogrammedControllers();

        // 5. 4-Stage vs 6-Stage Pipeline Hazard Forwarding & Stalls
        testPipelinedProcessor();

        // 6. Advanced Pipeline Datapath Optimizations (RAS, Store Forwarding, L1 Caches)
        testDatapathOptimizations();
    }

    // If external source file passed:
    if (!file_path.empty()) {
        StackPreprocessor prep;
        PreprocessorReport pr = prep.processFile(file_path);
        if (pr.success) {
            Assembler a;
            AssemblyResult r = a.assemble(pr.expanded_source);
            if (r.success) {
                std::cout << "\n=======================================================\n";
                std::cout << " Running File: " << file_path << " (Expanded: " << pr.macros_expanded << " macros)\n";
                std::cout << "=======================================================\n";

                bool do_4stage = (mode_str == "4" || mode_str == "both" || mode_str == "4stage");
                bool do_6stage = (mode_str == "6" || mode_str == "both" || mode_str == "6stage");

                CPUPipeline pipe4(PipelineMode::FOUR_STAGE);
                CPUPipeline pipe6(PipelineMode::SIX_STAGE);

                if (do_4stage) {
                    std::cout << "\n--- [4-STAGE PIPELINE EXECUTION: IF -> ID/RR -> EX -> MEM/WB] ---\n";
                    pipe4.loadProgram(r.machine_words, 0);
                    pipe4.run(50000);
                    std::cout << pipe4.dumpRegisters();
                    std::cout << pipe4.getMetrics().dump();
                    if (pipe4.hasException()) {
                        std::cout << " [HARDWARE EXCEPTION TRAPPED]: " << pipe4.getExceptionMessage() << "\n";
                    }
                }

                if (do_6stage) {
                    std::cout << "\n--- [6-STAGE SUPER-PIPELINE EXECUTION: IF1 -> IF2 -> OF -> EX -> MA -> RW] ---\n";
                    pipe6.loadProgram(r.machine_words, 0);
                    pipe6.run(50000);
                    std::cout << pipe6.dumpRegisters();
                    std::cout << pipe6.getMetrics().dump();
                    if (pipe6.hasException()) {
                        std::cout << " [HARDWARE EXCEPTION TRAPPED]: " << pipe6.getExceptionMessage() << "\n";
                    }
                }

                if (do_4stage && do_6stage) {
                    std::cout << "\n+-----------------------------------------------------------------------------------------------+\n";
                    std::cout << "|                      ARCHITECTURAL COMPARATIVE PIPELINE EVALUATION                            |\n";
                    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+-------+\n";
                    std::cout << "| Pipeline Model        | Clock Cycles  | Retired Inst  | Stalls/Flushes| CPI           | Speed |\n";
                    std::cout << "+-----------------------+---------------+---------------+---------------+---------------+-------+\n";
                    std::cout << "| 4-Stage Pipeline      | " << std::setw(13) << pipe4.getMetrics().total_cycles
                              << " | " << std::setw(13) << pipe4.getMetrics().instructions_retired
                              << " | " << std::setw(6) << pipe4.getMetrics().stall_cycles << " / " << std::setw(4) << pipe4.getMetrics().branch_flushes
                              << " | " << std::fixed << std::setprecision(3) << std::setw(13) << pipe4.getMetrics().getCPI()
                              << " | 1.00x |\n";

                    double speed_ratio = pipe4.getMetrics().getCPI() / pipe6.getMetrics().getCPI();
                    std::cout << "| 6-Stage Super-Pipeline| " << std::setw(13) << pipe6.getMetrics().total_cycles
                              << " | " << std::setw(13) << pipe6.getMetrics().instructions_retired
                              << " | " << std::setw(6) << pipe6.getMetrics().stall_cycles << " / " << std::setw(4) << pipe6.getMetrics().branch_flushes
                              << " | " << std::fixed << std::setprecision(3) << std::setw(13) << pipe6.getMetrics().getCPI()
                              << " | " << std::fixed << std::setprecision(2) << std::setw(5) << speed_ratio << "x |\n";
                    std::cout << "+-----------------------------------------------------------------------------------------------+\n";
                }
            }
        }
    }

    return 0;
}
