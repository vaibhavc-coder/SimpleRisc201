# SimpleRISC Simulator — SimpleRisc201
**Architecture Reference:** Smruti R. Sarangi, *Basic Computer Architecture* (Chapter 9: Microprogramming)  
**Submission Scope:** Core Architectural Foundation (ISA Specification, 32-Bit ALU, Assembler, Vertical Microprogrammed Control Unit)

---

## 📁 Package Contents

This repository contains the foundational components for the SimpleRISC processor:

```
├── include/
│   ├── types.hpp           # 32-bit SimpleRISC types, 21 opcodes, 19 internal registers, 45-bit microinstruction
│   ├── alu.hpp             # 32-bit ALU interface (arithmetic, logic, shifts, cmp flags E & GT)
│   ├── control_unit.hpp    # Vertical Microprogrammed Control Unit interface (Chapter 9)
│   └── assembler.hpp       # Two-Pass 32-bit SimpleRISC Assembler
├── src/
│   ├── alu.cpp             # 32-bit ALU implementation
│   ├── control_unit.cpp    # Microprogram ROM, shared bus arbitration, and uPC dispatch table
│   ├── assembler.cpp       # Two-pass machine word encoder (Table 9.2 & 9.3)
│   └── main.cpp            # Test driver verifying ALU, Assembler, and Control Unit
├── programs/
│   └── demo.s              # SimpleRISC assembly test program
├── Makefile                # Build script
├── COLLABORATION.md        # Team setup, branch guidelines, and workflow
└── README.md
```

---

## 🚀 How to Compile & Run

### 1. Build
```powershell
g++ -std=c++14 -O2 -I include src/alu.cpp src/control_unit.cpp src/assembler.cpp src/main.cpp -o eval1_demo.exe
```
*(or simply run `make` or press `Ctrl+Shift+B` in VS Code)*

### 2. Execute Demo
```powershell
.\eval1_demo.exe
```

---

## 🔍 Features & Architecture Highlights

1. **32-Bit ALU (`alu.cpp`):**
   - Arithmetic: `add`, `sub`, `mul`, `div`, `mod`.
   - Dedicated Comparison Flags: `cmp` setting `flags.E` (Equal) and `flags.GT` (Greater Than).

2. **32-Bit Assembler (`assembler.cpp`):**
   - Translates human-readable assembly mnemonics into 32-bit machine hex words following Table 9.2 and Table 9.3 formats.
   - Resolves forward and backward branch labels.

3. **Vertical Microprogrammed Control Unit (`control_unit.cpp`):**
   - Implements the **19 internal registers** (Table 9.7: `pc`, `ir`, `rd`, `rs1`, `rs2`, `immx`, `A`, `B`, `aluResult`, `mar`, `mdr`, etc.).
   - Implements the **8 microinstructions** (Table 9.8: `mloadIR`, `mdecode`, `mswitch`, `mmov`, `mmovi`, `madd`, `mbeq`, `mb`).
   - Models the **shared bus datapath** with multi-cycle micro-instruction execution, showing instruction retirement and micro-CPI (~10.7 cycles/instruction).

