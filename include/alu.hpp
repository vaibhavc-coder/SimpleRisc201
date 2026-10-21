#ifndef SIMPLERISC_ALU_HPP
#define SIMPLERISC_ALU_HPP

#include "types.hpp"
#include <string>

namespace simplerisc {

enum class ALUOp : uint8_t {
    ADD,    // a + b
    SUB,    // a - b
    MUL,    // a * b
    DIV,    // a / b
    MOD,    // a % b
    CMP,    // compare a and b -> sets flags.E and flags.GT
    AND,    // a & b
    OR,     // a | b
    NOT,    // ~b
    MOV,    // b
    LSL,    // a << (b & 0x1F)
    LSR,    // a >> (b & 0x1F) (logical)
    ASR     // a >> (b & 0x1F) (arithmetic)
};

struct ALUResult {
    word_t result{0};
    Flags flags;      // flags.E, flags.GT, flags.C, flags.V
};

class ALU {
public:
    static ALUResult execute(ALUOp op, word_t a, word_t b, const Flags& current_flags = Flags{});
    static ALUOp fromOpcode(Opcode op);
    static std::string opName(ALUOp op);
};

} // namespace simplerisc

#endif // SIMPLERISC_ALU_HPP
