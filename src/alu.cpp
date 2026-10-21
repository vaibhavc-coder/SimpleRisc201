#include "alu.hpp"
#include <stdexcept>

namespace simplerisc {

ALUResult ALU::execute(ALUOp op, word_t a, word_t b, const Flags& current_flags) {
    ALUResult res;
    res.flags = current_flags;

    uint64_t full_res = 0;
    sword_t sa = static_cast<sword_t>(a);
    sword_t sb = static_cast<sword_t>(b);

    switch (op) {
        case ALUOp::ADD:
            full_res = static_cast<uint64_t>(a) + static_cast<uint64_t>(b);
            res.result = static_cast<word_t>(full_res & 0xFFFFFFFFULL);
            res.flags.C = (full_res > 0xFFFFFFFFULL);
            res.flags.V = ((~(a ^ b) & (a ^ res.result)) & 0x80000000U) != 0;
            break;

        case ALUOp::SUB:
            full_res = static_cast<uint64_t>(a) - static_cast<uint64_t>(b);
            res.result = static_cast<word_t>(full_res & 0xFFFFFFFFULL);
            res.flags.C = (a >= b);
            res.flags.V = (((a ^ b) & (a ^ res.result)) & 0x80000000U) != 0;
            break;

        case ALUOp::MUL: {
            int64_t prod = static_cast<int64_t>(sa) * static_cast<int64_t>(sb);
            res.result = static_cast<word_t>(prod & 0xFFFFFFFFULL);
            res.flags.V = (prod > 0x7FFFFFFFLL || prod < -0x80000000LL);
            break;
        }

        case ALUOp::DIV:
            if (b == 0) {
                res.result = 0;
            } else {
                res.result = static_cast<word_t>(sa / sb);
            }
            break;

        case ALUOp::MOD:
            if (b == 0) {
                res.result = 0;
            } else {
                res.result = static_cast<word_t>(sa % sb);
            }
            break;

        case ALUOp::CMP:
            // SimpleRISC: cmp rs1, (rs2/imm)
            // Sets flags.E if equal, flags.GT if signed(rs1) > signed(rs2/imm)
            res.result = a - b;
            res.flags.E = (a == b);
            res.flags.GT = (sa > sb);
            res.flags.C = (a >= b);
            res.flags.V = (((a ^ b) & (a ^ res.result)) & 0x80000000U) != 0;
            break;

        case ALUOp::AND:
            res.result = a & b;
            break;

        case ALUOp::OR:
            res.result = a | b;
            break;

        case ALUOp::NOT:
            res.result = ~b;
            break;

        case ALUOp::MOV:
            res.result = b;
            break;

        case ALUOp::LSL: {
            uint32_t shift = b & 0x1F;
            res.result = a << shift;
            break;
        }

        case ALUOp::LSR: {
            uint32_t shift = b & 0x1F;
            res.result = a >> shift;
            break;
        }

        case ALUOp::ASR: {
            uint32_t shift = b & 0x1F;
            res.result = static_cast<word_t>(sa >> shift);
            break;
        }

    }

    return res;
}

ALUOp ALU::fromOpcode(Opcode op) {
    switch (op) {
        case OP_ADD: return ALUOp::ADD;
        case OP_SUB: return ALUOp::SUB;
        case OP_MUL: return ALUOp::MUL;
        case OP_DIV: return ALUOp::DIV;
        case OP_MOD: return ALUOp::MOD;
        case OP_CMP: return ALUOp::CMP;
        case OP_AND: return ALUOp::AND;
        case OP_OR:  return ALUOp::OR;
        case OP_NOT: return ALUOp::NOT;
        case OP_MOV: return ALUOp::MOV;
        case OP_LSL: return ALUOp::LSL;
        case OP_LSR: return ALUOp::LSR;
        case OP_ASR: return ALUOp::ASR;
        default:     return ALUOp::ADD;
    }
}

std::string ALU::opName(ALUOp op) {
    switch (op) {
        case ALUOp::ADD:    return "ADD";
        case ALUOp::SUB:    return "SUB";
        case ALUOp::MUL:    return "MUL";
        case ALUOp::DIV:    return "DIV";
        case ALUOp::MOD:    return "MOD";
        case ALUOp::CMP:    return "CMP";
        case ALUOp::AND:    return "AND";
        case ALUOp::OR:     return "OR";
        case ALUOp::NOT:    return "NOT";
        case ALUOp::MOV:    return "MOV";
        case ALUOp::LSL:    return "LSL";
        case ALUOp::LSR:    return "LSR";
        case ALUOp::ASR:    return "ASR";
    }
    return "UNKNOWN";
}

} // namespace simplerisc
