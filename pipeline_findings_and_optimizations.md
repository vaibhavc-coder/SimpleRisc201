# RISC201 Pipeline: Findings, Edge Cases, and Optimizations

This document tracks all observations, potential bugs, edge cases, and hardware/software optimization opportunities discovered during the code review and analysis of `src/cpu_pipeline.cpp` and `include/cpu_pipeline.hpp`, mapped against the CSC-201 Assignment Specification and Sarangi's *Basic Computer Architecture* (Chapter 9).

---

## Tracking Table

| ID | Category | Component / Location | Description / Issue / Opportunity | Recommended Action |
|:---:|:---|:---|:---|:---|
| **FIND-001** | Stack Boundary Config | `src/cpu_pipeline.cpp:32` | SP is initialized to `0x00003FFC` (16 KB boundary), while `types.hpp:22` defines `MEMORY_SIZE = 65536` (64 KB). | Align initial SP with `MEMORY_SIZE - 4` (`0x0000FFFC`) or make SP initialization parameterized by memory size. |
| **FIND-002** | Pipeline Microarchitecture | `cpu_pipeline.hpp`, `src/cpu_pipeline.cpp` | Upgraded 6-stage pipeline from merged `MEM/WB` to Sarangi canonical `IF1 -> IF2 -> OF -> EX -> MA -> RW`. | Successfully completed. Strictly separates memory access from register writeback, removes critical path bottlenecks, and provides clean 1-cycle load-use stalls. |
| **FIND-003** | Hazard Forwarding for `RA` | `types.hpp`, `cpu_pipeline.cpp` | Instructions writing `RA` (e.g. `CALL` saving `pc+4` or loads/moves to `r15`) were not forwarded to `RET` or operands reading `r15`. | Implemented `Instruction::readsRA()`, load-use stall detection for `RA`, and EX/MA + MA/RW forwarding for `CALL` return address and `RET` target resolution. |
| **FIND-004** | Cache & Structural Hazard Integration | `src/cpu_pipeline.cpp:731` | `IF2` stage was bypassing `icache_` directly to `readMemory()`, preventing I-Cache hit tracking and structural stall avoidance metrics. | Connected `icache_.access()` in `IF2` and added Harvard split-cache concurrent access detection when `MA` and `IF2` access memory simultaneously. |
| **FIND-005** | Architectural Comparative Suite | `src/main.cpp:353` | Program was restricted to running files only in 4-stage mode with hardcoded pipeline selection. | Added `--mode <4|6|both>`, `--no-tests`, and dual pipeline evaluation table printing side-by-side execution metrics. |

---

## Detailed Notes & Discoveries

### FIND-001: Stack Pointer (SP) Initial Address vs Memory Size
- **Location:** [`src/cpu_pipeline.cpp:32`](file:///home/omkiran/iit%20roorkee/coa%20project/src/cpu_pipeline.cpp#L32)
- **Observation:** `regs_[SP] = 0x00003FFC; // Default SP top of 16KB`.
- **Context:** In `types.hpp`, `MEMORY_SIZE` was expanded to 64 KB (`65536` bytes). Initializing SP to `0x3FFC` only grants 16 KB of address space below the stack before hitting 0, and leaves the upper 48 KB (`0x4000` to `0xFFFF`) unused.
- **Impact:** Harmless for small programs, but could restrict stack growth for deep recursion or large stack frames.

---

### FIND-002: Transition to Canonical 6-Stage Super-Pipeline (`IF1 -> IF2 -> OF -> EX -> MA -> RW`)
- **Location:** [`include/cpu_pipeline.hpp`](file:///home/omkiran/iit%20roorkee/coa%20project/include/cpu_pipeline.hpp) & [`src/cpu_pipeline.cpp`](file:///home/omkiran/iit%20roorkee/coa%20project/src/cpu_pipeline.cpp)
- **Problem Solved:** Merging Memory Access and Register Write into a single stage forced Data Cache latency and register write setup time into the same cycle, limiting clock frequency.
- **Implemented Architecture:**
  1. `IF1`: Program Counter & Address Generation
  2. `IF2`: Instruction Memory / Cache Access
  3. `OF`: Operand Fetch (Instruction Decode, Register Read, 1-cycle Load-Use Hazard Stall, Write-before-Read Forwarding)
  4. `EX`: ALU Execution, Operand Forwarding from `EX/MA` and `MA/RW`, Branch Evaluation
  5. `MA`: Data Memory Access (with Store-Load Bypassing & D-Cache)
  6. `RW`: Register File Commit / Writeback
- **Result:** 100% verified against test benchmarks with 0 assertion failures.

---

### FIND-003: Subroutine Call Return Address (`RA`) Hazard Bypass & Forwarding
- **Location:** [`src/cpu_pipeline.cpp`](file:///home/omkiran/iit%20roorkee/coa%20project/src/cpu_pipeline.cpp) & [`include/types.hpp`](file:///home/omkiran/iit%20roorkee/coa%20project/include/types.hpp)
- **Problem Solved:** `OP_CALL` implicitly writes `RA = pc + 4`, but did not set `writesRd()`. As a result, subsequent instructions reading `RA` or `OP_RET` returning immediately after restoring `RA` could read a stale register value.
- **Implemented Fix:**
  - Added `Instruction::readsRA()` identifying `OP_RET`.
  - Added `RA` load-use stall detection in `ID/RR` (4-stage) and `OF` (6-stage).
  - Added `CALL` return address forwarding in `EX` stage forwarding paths and `RET` target resolution.
  - Added `CALL` bypass in `ID/RR` and `OF` write-before-read internal forwarding.

---

### FIND-004: L1 Cache & Structural Hazard Tracking in 6-Stage Pipeline
- **Location:** [`src/cpu_pipeline.cpp:731`](file:///home/omkiran/iit%20roorkee/coa%20project/src/cpu_pipeline.cpp#L731)
- **Problem Solved:** `IF2` directly accessed raw memory without querying the L1 Instruction Cache, preventing cache telemetry from updating during 6-stage execution.
- **Implemented Fix:** Routed `IF2` through `icache_.access()`, and added structural hazard avoidance detection when `IF2` and `MA` execute memory requests concurrently on the split Harvard architecture.

---

### FIND-005: Flexible Pipeline Execution Modes & Comparative Evaluation
- **Location:** [`src/main.cpp`](file:///home/omkiran/iit%20roorkee/coa%20project/src/main.cpp)
- **Enhancement:** Allows running assembly programs in 4-stage mode (`--mode 4`), 6-stage super-pipeline mode (`--mode 6`), or comparative dual mode (`--mode both`), producing a side-by-side performance evaluation table with clock cycles, instructions retired, stalls, flushes, CPI, and relative speedup.

