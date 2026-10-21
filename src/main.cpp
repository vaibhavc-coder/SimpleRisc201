#include <iostream>
#include <iomanip>
#include <cassert>
#include <string>
#include "types.hpp"
#include "alu.hpp"
#include "control_unit.hpp"
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
    std::cout << "\n=======================================================\n";
    std::cout << " 1. TESTING 32-BIT ALU (Smruti Sarangi Architecture)\n";
    std::cout << "=======================================================\n";

    ALUResult add_res = ALU::execute(ALUOp::ADD, 15, 25);
    std::cout << "[ALU ADD] 15 + 25 = " << add_res.result << " (Expected: 40)\n";

    ALUResult sub_res = ALU::execute(ALUOp::SUB, 50, 20);
    std::cout << "[ALU SUB] 50 - 20 = " << sub_res.result << " (Expected: 30)\n";

    ALUResult mul_res = ALU::execute(ALUOp::MUL, 6, 7);
    std::cout << "[ALU MUL] 6 * 7   = " << mul_res.result << " (Expected: 42)\n";

    ALUResult cmp_eq = ALU::execute(ALUOp::CMP, 100, 100);
    std::cout << "[ALU CMP] cmp 100, 100 -> flags.E=" << cmp_eq.flags.E
              << " flags.GT=" << cmp_eq.flags.GT << " (Expected: E=1, GT=0)\n";

    ALUResult cmp_gt = ALU::execute(ALUOp::CMP, 200, 50);
    std::cout << "[ALU CMP] cmp 200, 50  -> flags.E=" << cmp_gt.flags.E
              << " flags.GT=" << cmp_gt.flags.GT << " (Expected: E=0, GT=1)\n";
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
    ControlUnit vertical_cu;
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

    assert(pipe6.getRegister(7) == 40);
    assert(pipe6.getRegister(8) == 0);
    assert(pipe6.getRegister(9) == 50);
    std::cout << "[PASS] 6-Stage Super-Pipeline Execution Verified!\n";

    std::cout << "\n===============================================================================\n";
    std::cout << " 7. ARCHITECTURAL COMPARATIVE EVALUATION\n";
    std::cout << "===============================================================================\n";

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
    // Check if CLI interactive debugger mode requested
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli" || arg == "-i" || arg == "--debugger") {
            CPUPipeline pipeline(PipelineMode::FOUR_STAGE);
            // If a file is also passed, load it
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                Assembler a;
                AssemblyResult r = a.assembleFile(argv[i + 1]);
                if (r.success) {
                    pipeline.loadProgram(r.machine_words, 0);
                    std::cout << "Loaded program " << argv[i + 1] << " (" << r.machine_words.size() << " instructions)\n";
                }
            }
            DebuggerCLI dbg(pipeline);
            dbg.runInteractive();
            return 0;
        }
    }

    std::cout << "===============================================================================\n";
    std::cout << "   RISC201 COMPLETE INTEGRATED PROCESSOR SIMULATION & EVALUATION SUITE        \n";
    std::cout << "   Computer Organization & Architecture (CSC-201) — Autumn 2026               \n";
    std::cout << "   Reference: Smruti R. Sarangi (Basic Computer Architecture, Chapter 9)       \n";
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

    // If external source file passed:
    if (argc >= 2 && argv[1][0] != '-') {
        StackPreprocessor prep;
        PreprocessorReport pr = prep.processFile(argv[1]);
        if (pr.success) {
            Assembler a;
            AssemblyResult r = a.assemble(pr.expanded_source);
            if (r.success) {
                std::cout << "\n=======================================================\n";
                std::cout << " Running File: " << argv[1] << " (Expanded: " << pr.macros_expanded << " macros)\n";
                std::cout << "=======================================================\n";
                CPUPipeline file_pipe(PipelineMode::FOUR_STAGE);
                file_pipe.loadProgram(r.machine_words, 0);
                file_pipe.run(50000);
                std::cout << file_pipe.dumpRegisters();
                std::cout << file_pipe.getMetrics().dump();
            }
        }
    }

    return 0;
}
