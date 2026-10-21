#include "debugger_cli.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace simplerisc {

DebuggerCLI::DebuggerCLI(CPUPipeline& pipeline)
    : pipeline_(pipeline), exceptions_(StackBounds{}) {}

void DebuggerCLI::printBanner(std::ostream& out) {
    out << "===============================================================================\n";
    out << "       RISC201 INTERACTIVE CPU EXECUTION SIMULATOR & CLI DEBUGGER             \n";
    out << "  Smruti R. Sarangi Architecture: 4-Stage / 6-Stage Pipeline & Microprogramming\n";
    out << "===============================================================================\n";
    out << "Type 'help' or 'h' for a list of available debugger commands.\n\n";
}

void DebuggerCLI::printHelp(std::ostream& out) {
    out << "+-----------------------------------------------------------------------------+\n";
    out << "|                      RISC201 DEBUGGER COMMAND MENU                          |\n";
    out << "+-----------------------------------------------------------------------------+\n";
    out << "| step [n] / s [n]    : Advance simulation by n clock / micro cycles (def: 1) |\n";
    out << "| next / n            : Macro-step (run until current instruction writeback)  |\n";
    out << "| run / c / continue  : Run continuously until breakpoint, halt, or exception |\n";
    out << "| break <pc> / b <pc> : Set a breakpoint at PC address (hex or decimal)       |\n";
    out << "| del <pc>            : Delete breakpoint at specified address                |\n";
    out << "| breaks              : Display list of all active breakpoints               |\n";
    out << "| regs / r            : Dump complete register file (r0-r15), PC, and Flags   |\n";
    out << "| stack [d] / st [d]  : Render ASCII active stack memory frame (depth d words)|\n";
    out << "| pipe / p            : Display pipeline hardware latches, stalls, & bypasses |\n";
    out << "| micro / u           : Inspect Microprogrammed Control Unit (uPC & ROM)     |\n";
    out << "| mem <addr> [count]  : Dump memory contents starting at byte address         |\n";
    out << "| disasm [pc] [cnt]   : Disassemble machine instructions from memory          |\n";
    out << "| mode <4|6|micro|hz> : Switch pipeline mode (4stage, 6stage, vertical, horiz)|\n";
    out << "| trace [on|off]      : Toggle cycle-by-cycle telemetry printouts             |\n";
    out << "| reset               : Reset CPU registers, pipeline latches, and memory     |\n";
    out << "| help / h            : Show this help menu                                   |\n";
    out << "| quit / q            : Exit the interactive debugger session                 |\n";
    out << "+-----------------------------------------------------------------------------+\n";
}

void DebuggerCLI::printCurrentState(std::ostream& out) {
    out << "PC: 0x" << std::hex << std::setw(8) << std::setfill('0') << pipeline_.getPC()
        << " | SP: 0x" << std::setw(8) << std::setfill('0') << pipeline_.getRegister(SP)
        << " | Flags: [E=" << pipeline_.getFlags().E << " GT=" << pipeline_.getFlags().GT << "]";
    word_t w = pipeline_.readMemory(pipeline_.getPC());
    DisassembledInstruction d = Disassembler::disassemble(w, pipeline_.getPC());
    out << " | Next Inst: 0x" << std::setw(8) << std::setfill('0') << w << " (" << d.full_assembly << ")\n";
}

bool DebuggerCLI::executeCommand(const std::string& cmd_line, std::ostream& out) {
    std::stringstream ss(cmd_line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty()) return true;

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::tolower(c); });

    if (cmd == "q" || cmd == "quit" || cmd == "exit") {
        quit_ = true;
        out << "Exiting RISC201 Debugger. Goodbye!\n";
        return false;
    }

    if (cmd == "h" || cmd == "help") {
        printHelp(out);
        return true;
    }

    if (cmd == "s" || cmd == "step") {
        uint32_t count = 1;
        ss >> count;
        for (uint32_t i = 0; i < count; ++i) {
            if (pipeline_.isHalted()) {
                out << "CPU is halted.\n";
                break;
            }
            pipeline_.cycleStep();
            // Verify stack boundary safety on every cycle
            addr_t sp = pipeline_.getRegister(SP);
            addr_t pc = pipeline_.getPC();
            if (!exceptions_.checkStackPointer(sp, pc)) {
                out << exceptions_.dumpExceptionReport();
                break;
            }
            if (trace_mode_) {
                out << pipeline_.dumpPipelineState();
            }
            if (breakpoints_.count(pipeline_.getPC())) {
                out << "Breakpoint hit at PC 0x" << std::hex << pipeline_.getPC() << "\n";
                break;
            }
        }
        printCurrentState(out);
        return true;
    }

    if (cmd == "n" || cmd == "next") {
        if (pipeline_.isHalted()) {
            out << "CPU is halted.\n";
        } else {
            pipeline_.macroStep();
            printCurrentState(out);
        }
        return true;
    }

    if (cmd == "c" || cmd == "run" || cmd == "continue") {
        out << "Running simulation...\n";
        uint64_t cycles = 0;
        while (!pipeline_.isHalted() && cycles < 1000000) {
            pipeline_.cycleStep();
            cycles++;
            addr_t sp = pipeline_.getRegister(SP);
            addr_t pc = pipeline_.getPC();
            if (!exceptions_.checkStackPointer(sp, pc, cycles)) {
                out << exceptions_.dumpExceptionReport();
                break;
            }
            if (breakpoints_.count(pipeline_.getPC())) {
                out << "Breakpoint hit at PC 0x" << std::hex << pipeline_.getPC() << " after " << std::dec << cycles << " cycles.\n";
                break;
            }
        }
        if (pipeline_.isHalted()) {
            out << "Program finished / Halted after " << std::dec << cycles << " cycles.\n";
        }
        printCurrentState(out);
        return true;
    }

    if (cmd == "b" || cmd == "break") {
        std::string addr_token;
        if (ss >> addr_token) {
            addr_t addr = 0;
            if (addr_token.size() > 2 && (addr_token[0] == '0' && (addr_token[1] == 'x' || addr_token[1] == 'X'))) {
                addr = static_cast<addr_t>(std::stoul(addr_token.substr(2), nullptr, 16));
            } else {
                addr = static_cast<addr_t>(std::stoul(addr_token));
            }
            breakpoints_.insert(addr);
            out << "Breakpoint " << breakpoints_.size() << " set at 0x" << std::hex << addr << "\n";
        } else {
            out << "Usage: break <pc_address>\n";
        }
        return true;
    }

    if (cmd == "del") {
        std::string addr_token;
        if (ss >> addr_token) {
            addr_t addr = 0;
            if (addr_token.size() > 2 && (addr_token[0] == '0' && (addr_token[1] == 'x' || addr_token[1] == 'X'))) {
                addr = static_cast<addr_t>(std::stoul(addr_token.substr(2), nullptr, 16));
            } else {
                addr = static_cast<addr_t>(std::stoul(addr_token));
            }
            if (breakpoints_.erase(addr)) {
                out << "Breakpoint removed from 0x" << std::hex << addr << "\n";
            } else {
                out << "No breakpoint found at 0x" << std::hex << addr << "\n";
            }
        }
        return true;
    }

    if (cmd == "breaks") {
        out << "Active Breakpoints:\n";
        if (breakpoints_.empty()) {
            out << "  (none)\n";
        } else {
            for (addr_t bp : breakpoints_) {
                out << "  - 0x" << std::hex << std::setw(8) << std::setfill('0') << bp << "\n";
            }
        }
        return true;
    }

    if (cmd == "r" || cmd == "regs") {
        out << pipeline_.dumpRegisters();
        return true;
    }

    if (cmd == "st" || cmd == "stack") {
        size_t depth = 10;
        ss >> depth;
        std::vector<word_t> mem_snapshot;
        for (addr_t a = 0; a < 65536; a += 4) {
            mem_snapshot.push_back(pipeline_.readMemory(a));
        }
        out << StackVisualizer::renderWithContext(
            mem_snapshot,
            pipeline_.getRegister(SP),
            pipeline_.getPC(),
            pipeline_.getRegister(RA),
            0x00003FFC,
            0x00002000,
            depth
        );
        return true;
    }

    if (cmd == "p" || cmd == "pipe") {
        out << pipeline_.dumpPipelineState();
        return true;
    }

    if (cmd == "u" || cmd == "micro") {
        out << horiz_cu_.dumpControlState();
        return true;
    }

    if (cmd == "m" || cmd == "mem") {
        std::string addr_str;
        uint32_t count = 4;
        if (ss >> addr_str) {
            addr_t start = 0;
            if (addr_str.size() > 2 && (addr_str[0] == '0' && (addr_str[1] == 'x' || addr_str[1] == 'X'))) {
                start = static_cast<addr_t>(std::stoul(addr_str.substr(2), nullptr, 16));
            } else {
                start = static_cast<addr_t>(std::stoul(addr_str));
            }
            ss >> count;
            out << "Memory Dump at 0x" << std::hex << start << ":\n";
            for (uint32_t i = 0; i < count; ++i) {
                addr_t cur = start + i * 4;
                word_t val = pipeline_.readMemory(cur);
                out << "  0x" << std::hex << std::setw(8) << std::setfill('0') << cur << " : 0x"
                    << std::setw(8) << std::setfill('0') << val << " (" << std::dec << val << ")\n";
            }
        } else {
            out << "Usage: mem <start_address> [word_count]\n";
        }
        return true;
    }

    if (cmd == "d" || cmd == "disasm") {
        addr_t start = pipeline_.getPC();
        uint32_t count = 8;
        std::string addr_str;
        if (ss >> addr_str) {
            if (addr_str.size() > 2 && (addr_str[0] == '0' && (addr_str[1] == 'x' || addr_str[1] == 'X'))) {
                start = static_cast<addr_t>(std::stoul(addr_str.substr(2), nullptr, 16));
            } else {
                start = static_cast<addr_t>(std::stoul(addr_str));
            }
            ss >> count;
        }
        std::vector<DisassembledInstruction> dis_list;
        for (uint32_t i = 0; i < count; ++i) {
            addr_t a = start + i * 4;
            word_t w = pipeline_.readMemory(a);
            dis_list.push_back(Disassembler::disassemble(w, a));
        }
        out << Disassembler::formatListing(dis_list);
        return true;
    }

    if (cmd == "mode") {
        std::string m;
        if (ss >> m) {
            if (m == "4" || m == "4stage") {
                pipeline_.setPipelineMode(PipelineMode::FOUR_STAGE);
                out << "Switched to 4-Stage Pipeline Mode.\n";
            } else if (m == "6" || m == "6stage") {
                pipeline_.setPipelineMode(PipelineMode::SIX_STAGE);
                out << "Switched to 6-Stage Super-Pipeline Mode.\n";
            } else if (m == "micro" || m == "vertical") {
                pipeline_.setPipelineMode(PipelineMode::MICROPROGRAMMED);
                out << "Switched to Vertical Microprogrammed Model.\n";
            } else {
                out << "Unknown mode: " << m << ". Valid: 4stage, 6stage, micro\n";
            }
        }
        return true;
    }

    if (cmd == "trace") {
        std::string opt;
        if (ss >> opt) {
            trace_mode_ = (opt == "on" || opt == "1" || opt == "true");
        } else {
            trace_mode_ = !trace_mode_;
        }
        out << "Cycle tracing is now " << (trace_mode_ ? "ENABLED" : "DISABLED") << "\n";
        return true;
    }

    if (cmd == "reset") {
        pipeline_.reset();
        exceptions_.clear();
        out << "CPU Pipeline, Registers, and Memory reset to initial power-on state.\n";
        return true;
    }

    out << "Unknown command: '" << cmd << "'. Type 'help' for command list.\n";
    return true;
}

void DebuggerCLI::runInteractive(std::istream& in, std::ostream& out) {
    printBanner(out);
    printCurrentState(out);

    std::string line;
    while (!quit_) {
        out << "(risc201-dbg) ";
        if (!std::getline(in, line)) break;
        executeCommand(line, out);
    }
}

} // namespace simplerisc
