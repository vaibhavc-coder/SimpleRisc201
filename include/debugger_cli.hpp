#ifndef SIMPLERISC_DEBUGGER_CLI_HPP
#define SIMPLERISC_DEBUGGER_CLI_HPP

#include "cpu_pipeline.hpp"
#include "horizontal_control_unit.hpp"
#include "disassembler.hpp"
#include "stack_visualizer.hpp"
#include "exception_framework.hpp"
#include <string>
#include <set>
#include <iostream>

namespace simplerisc {

class DebuggerCLI {
public:
    explicit DebuggerCLI(CPUPipeline& pipeline);

    // Launch interactive command line debugger loop
    void runInteractive(std::istream& in = std::cin, std::ostream& out = std::cout);

    // Single command execution (useful for scripted or test calls)
    bool executeCommand(const std::string& cmd_line, std::ostream& out = std::cout);

    void setBreakpoints(const std::set<addr_t>& bps) { breakpoints_ = bps; }

private:
    void printBanner(std::ostream& out);
    void printHelp(std::ostream& out);
    void printCurrentState(std::ostream& out);

    CPUPipeline& pipeline_;
    HorizontalControlUnit horiz_cu_;
    ExceptionFramework exceptions_;
    std::set<addr_t> breakpoints_;
    bool quit_{false};
    bool trace_mode_{false};
};

} // namespace simplerisc

#endif // SIMPLERISC_DEBUGGER_CLI_HPP
