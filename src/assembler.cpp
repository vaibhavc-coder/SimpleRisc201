#include "assembler.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace simplerisc {

// --- Helper Functions ---
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return str.substr(first, str.find_last_not_of(" \t\r\n") - first + 1);
}

static std::string toUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

static std::vector<std::string> splitTokens(const std::string& str) {
    std::vector<std::string> tokens;
    size_t prev = 0, pos;
    while ((pos = str.find_first_of(" ,\t\r\n()[]", prev)) != std::string::npos) {
        if (pos > prev) tokens.push_back(str.substr(prev, pos - prev));
        prev = pos + 1;
    }
    if (prev < str.length()) tokens.push_back(str.substr(prev));
    return tokens;
}

// --- Opcode Lookup Table ---
static const std::unordered_map<std::string, Opcode> opMap = {
    {"NOP", OP_NOP}, {"RET", OP_RET}, {"B", OP_B}, {"BEQ", OP_BEQ}, 
    {"BGT", OP_BGT}, {"CALL", OP_CALL}, {"CMP", OP_CMP}, {"MOV", OP_MOV}, 
    {"NOT", OP_NOT}, {"LD", OP_LD}, {"ST", OP_ST}, {"ADD", OP_ADD}, 
    {"SUB", OP_SUB}, {"MUL", OP_MUL}, {"DIV", OP_DIV}, {"MOD", OP_MOD}, 
    {"AND", OP_AND}, {"OR", OP_OR}, {"LSL", OP_LSL}, {"LSR", OP_LSR}, {"ASR", OP_ASR}
};

// --- Assembler Class Implementation ---
void Assembler::reset() {
    symbol_table_.clear();
}

bool Assembler::parseRegister(const std::string& token, reg_t& out_reg) {
    std::string upper = toUpper(trim(token));
    if (upper == "SP" || upper == "$SP" || upper == "R14" || upper == "$14") { out_reg = 14; return true; }
    if (upper == "RA" || upper == "$RA" || upper == "R15" || upper == "$15") { out_reg = 15; return true; }
    if (upper.size() >= 2 && (upper[0] == 'R' || upper[0] == '$')) {
        try {
            int num = std::stoi(upper.substr(1));
            if (num >= 0 && num <= 15) { out_reg = static_cast<reg_t>(num); return true; }
        } catch (...) {}
    }
    return false;
}

bool Assembler::parseImmediate(const std::string& token, int32_t& out_imm) {
    std::string clean = trim(token);
    if (clean.empty()) return false;
    try {
        if (clean.size() > 2 && (clean.substr(0, 2) == "0x" || clean.substr(0, 2) == "0X")) {
            out_imm = static_cast<int32_t>(std::stoul(clean, nullptr, 16));
        } else {
            out_imm = std::stol(clean, nullptr, 10);
        }
        return true;
    } catch (...) { return false; }
}

AssemblyResult Assembler::assembleFile(const std::string& filepath) {
    std::ifstream file(filepath); //attmepts to open the file path
    if (!file.is_open()) return {false, "Could not open file: " + filepath, {}, {}}; 
    std::stringstream buffer;
    buffer << file.rdbuf();  //entire file is coppied into buffer(temporary memory) in a efficient way
    return assemble(buffer.str()); //will send the file and return whatever the Assembly sends it
}

AssemblyResult Assembler::assemble(const std::string& raw_source) {
    reset();
    AssemblyResult result;
    result.success = true;
    // there will be two scans one just all the instructions will be stored in parsed_instructions
    // we are using two scans because a label might be called but It may not still be processed
    // (if the label is after the caller)
    std::vector<std::vector<std::string>> parsed_instructions;
    //parsed instructions will store all the instruction and each instruction will further be decoded
    // to the tokens each tojen is ike part of instruction "add", "rd" etc
    addr_t current_pc = TEXT_BASE;  //present in types.hpp, the base of all instruction set

    // PASS 1: Clean text, build symbol table, and extract tokens efficiently
    // -> it starts a program counter at text base
    // -> it strips out comments anything after ';'
    // -> all the labels are stored in the symbol table along with their pc values
    // -> for every instruction it increases the pc by 4
    std::stringstream ss(raw_source);
    std::string line;
    while (std::getline(ss, line)) {
        line = line.substr(0, line.find(';')); // Strip comments
        
        size_t colon = line.find(':');
        if (colon != std::string::npos) {      // Extract Label, npos: it is string with
            //very large value as we cant store that large value so it just make it as 'not found'
            //so so the string colon is just getting compared with 'not found'
            symbol_table_[trim(line.substr(0, colon))] = current_pc;
            line = line.substr(colon + 1);
        }
        
        auto tokens = splitTokens(line);
        if (!tokens.empty() && tokens[0] != ".text" && tokens[0] != ".data") { //general take care
            // like .text and .data will be present in all assembly files so to prevent 
            // that as a instruction we wont push it to parsed_instruction
            parsed_instructions.push_back(tokens);
            current_pc += 4;
        }
    }
    // if we have all the register and opcode we use this function to create binary instruction
    // Encoding helpers
    auto encodeReg = [](Opcode op, reg_t rd, reg_t rs1, reg_t rs2) -> word_t {
        return ((op & 0x1F) << 27) | (0U << 26) | ((rd & 0xF) << 22) | ((rs1 & 0xF) << 18) | ((rs2 & 0xF) << 14);
    };
    auto encodeImm = [](Opcode op, reg_t rd, reg_t rs1, uint16_t imm16) -> word_t {
        return ((op & 0x1F) << 27) | (1U << 26) | ((rd & 0xF) << 22) | ((rs1 & 0xF) << 18) | (imm16 & 0xFFFF);
    };

    // PASS 2: Machine words generation
    // making current pc to base to do second scan
    current_pc = TEXT_BASE;
    for (const auto& tokens : parsed_instructions) {
        std::string mnem = toUpper(tokens[0]); //the frist token all the operations etc
        Opcode op = opMap.count(mnem) ? opMap.at(mnem) : OP_ADD;  //default add is random word is given
        word_t machine_word = 0;   // the final instruction
        reg_t r1 = 0, r2 = 0, r3 = 0;
        int32_t imm = 0;

        // Group 1: No arguments
        if (mnem == "NOP" || mnem == "RET") {
            machine_word = (static_cast<word_t>(op) << 27);
        } 
        // Group 2: Branches
        // call .loop this is the instruction we need to decode it so call will first be found
        // the label address will be stored in symbol_table so we will find the offset then divided
        // 4, offset will be then be stored
        else if (mnem == "B" || mnem == "BEQ" || mnem == "BGT" || mnem == "CALL") {
            if (tokens.size() >= 2) {
                if (symbol_table_.count(tokens[1])) {
                    imm = (symbol_table_[tokens[1]] - current_pc) / 4;
                } else {
                    parseImmediate(tokens[1], imm);
                }
            }
            machine_word = ((op & 0x1F) << 27) | (imm & 0x07FFFFFF);
        } 


        // Group 3: Memory (Load/Store)

        else if (mnem == "LD" || mnem == "ST") {
            if (tokens.size() >= 3 && parseRegister(tokens[1],r1)) {
                if (tokens.size() >= 4) { // E.g., LD R1, 0x10(R2)
                    parseImmediate(tokens[2], imm);
                    parseRegister(tokens[3], r2);
                } else {                  // E.g., LD R1, R2
                    parseRegister(tokens[2], r2);
                }
                machine_word = encodeImm(op, r1, r2, imm);
            }
        } 
        // Group 4: 2 Arguments (CMP, MOV, NOT)
        else if (mnem == "CMP" || mnem == "MOV" || mnem == "NOT") {
            reg_t rd = (mnem == "CMP") ? 0 : r1; // CMP has no destination register
            if (tokens.size() >= 3 && parseRegister(tokens[1], r1)) {
                rd = (mnem == "CMP") ? 0 : r1;
                if (parseRegister(tokens[2], r2)) machine_word = encodeReg(op, rd, (mnem=="CMP")?r1:0, r2);
                else { parseImmediate(tokens[2], imm); machine_word = encodeImm(op, rd, (mnem=="CMP")?r1:0, imm); }
            }
        } 
        // Group 5: 3 Arguments (ALU Operations: ADD, SUB, MUL, etc.)
        else {
            if (tokens.size() >= 4 && parseRegister(tokens[1], r1) && parseRegister(tokens[2], r2)) {
                if (parseRegister(tokens[3], r3)){
                    machine_word = encodeReg(op, r1, r2, r3);
                }
                else { 
                    parseImmediate(tokens[3], imm); 
                    machine_word = encodeImm(op, r1, r2, imm); 
                }
            }
        }
        result.machine_words.push_back(machine_word);
        current_pc += 4;
    }
    result.symbols = symbol_table_;
    return result;
}

}