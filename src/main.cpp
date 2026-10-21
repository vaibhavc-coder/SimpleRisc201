#include <iostream>
#include <iomanip>
#include "types.hpp"
#include "alu.hpp"
#include "control_unit.hpp"
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

int main(int argc, char* argv[]) {
    std::cout << "=======================================================\n";
    std::cout << " SimpleRISC Evaluation 1 Submission Demo\n";
    std::cout << " Components: ISA Types | 32-Bit ALU | Assembler | Micro-CU\n";
    std::cout << " Reference: Smruti R. Sarangi (Basic Computer Architecture)\n";
    std::cout << "=======================================================\n";

    // 1. Test ALU
    testALU();

    // 2. Sample Assembly Program
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

    // If a file was passed as argument, load that instead
    if (argc >= 2) {
        Assembler a;
        AssemblyResult r = a.assembleFile(argv[1]);
        if (r.success) {
            std::cout << "\nLoaded source file: " << argv[1] << "\n";
            testAssemblerAndControlUnit(sample_program);
            return 0;
        }
    }

    testAssemblerAndControlUnit(sample_program);
    return 0;
}
