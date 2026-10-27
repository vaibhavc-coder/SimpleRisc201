# RISC201 Architectural Innovations & Hardware Optimizations

This document records all architectural enhancements, advanced arithmetic circuits, and hardware algorithms implemented in the **RISC201 (SimpleRISC)** processor simulator that go beyond baseline textbook designs.

---

## Table of Contents
1. [ALU Optimization 1: Radix-4 Modified Booth's Multiplier](#1-32-bit-radix-4-modified-booths-multiplier)
2. [ALU Optimization 2: Hierarchical Carry-Lookahead Adder with GPK Logic](#2-hierarchical-32-bit-cla-adder-with-generate-propagate-and-kill-gpk-logic)
3. [ALU Optimization 3: Double-Precision Newton-Raphson Integer Division & Modulo](#3-double-precision-newton-raphson-integer-division--modulo)
4. [ALU Optimization 4: IEEE-754 Single-Precision Floating-Point Multiplier (Figure 8.18)](#4-ieee-754-single-precision-floating-point-multiplier-figure-818)
5. [ALU Optimization 5: IEEE-754 Single-Precision Floating-Point Divider via Newton-Raphson](#5-ieee-754-single-precision-floating-point-divider-via-newton-raphson)
6. [Architectural Flags & Microarchitecture Telemetry Extension](#6-architectural-flags--microarchitecture-telemetry-extension)
7. [ISA Extension: UNARY_REG Format & Extended 24-Opcode Architecture](#7-isa-extension-unary_reg-format--extended-24-opcode-architecture)
8. [Hardware Bit-Level Integer <-> Double Conversions via Priority-Encoder CLZ](#8-hardware-bit-level-integer---double-conversions-via-priority-encoder-clz)
9. [Architectural Trade-Off & Performance Review](#9-architectural-trade-off--performance-review)

---

## 1. 32-Bit Radix-4 Modified Booth's Multiplier

### Background & Motivation
In standard shift-and-add multipliers, multiplying two 32-bit numbers requires 32 sequential iterations (one partial product generated per bit). Additionally, signed 2's complement multiplication requires special sign-extension handling or pre-converting to absolute values.

### Hardware Algorithm & Theory
Radix-4 Modified Booth recoding inspects **3 overlapping multiplier bits** at a time $(B_{2i+1}, B_{2i}, B_{2i-1})$ with an initial boundary condition $B_{-1} = 0$.

* **Partial Product Reduction**: Iterations are reduced by **50%**, requiring exactly $\lceil 32 / 2 \rceil = 16$ iterations.
* **Native Signed Support**: Handles positive and negative numbers directly in 2's complement without magnitude conversion.

#### Recoding Truth Table:
| $B_{2i+1}$ | $B_{2i}$ | $B_{2i-1}$ | Operation on Multiplicand $A$ | Hardware Action |
|:---:|:---:|:---:|:---:|:---|
| 0 | 0 | 0 | $+0 \times A$ | No addition |
| 0 | 0 | 1 | $+1 \times A$ | Add $A$ |
| 0 | 1 | 0 | $+1 \times A$ | Add $A$ |
| 0 | 1 | 1 | $+2 \times A$ | Add $A \ll 1$ |
| 1 | 0 | 0 | $-2 \times A$ | Add $-(A \ll 1)$ (2's complement of $A \ll 1$) |
| 1 | 0 | 1 | $-1 \times A$ | Add $-A$ (2's complement of $A$) |
| 1 | 1 | 0 | $-1 \times A$ | Add $-A$ (2's complement of $A$) |
| 1 | 1 | 1 | $+0 \times A$ | No addition |

#### Mathematical Formulation:
$$\text{Product} = \sum_{i=0}^{15} \left( -2 B_{2i+1} + B_{2i} + B_{2i-1} \right) \cdot A \cdot 2^{2i}$$

#### Hardware Implementation Details:
* **File**: `src/alu.cpp` $\rightarrow$ `ALU::boothMultiply32`
* Accumulator width: 64 bits to prevent intermediate overflow.
* Signed Overflow Detection ($V$): Evaluates if the upper 32 bits of the 64-bit product are not a pure sign-extension of the lower 32-bit result ($V = (\text{Accumulator} \neq \text{SignExtend}(\text{Result}_{31:0}))$.

---

## 2. Hierarchical 32-Bit CLA Adder with Generate, Propagate, and Kill (GPK) Logic

### Background & Motivation
A 32-bit Ripple Carry Adder (RCA) incurs a worst-case latency of $32 \Delta$ gate delays due to sequential carry propagation from bit 0 to bit 31.

### Hardware Algorithm & Theory
The Carry-Lookahead Adder (CLA) computes carry signals in parallel across all bit positions using three mutually exclusive states:
1. **Generate ($G_i$)**: $A_i \cdot B_i$ $\rightarrow$ Generates carry regardless of carry-in ($C_{i+1} = 1$).
2. **Propagate ($P_i$)**: $A_i \oplus B_i$ $\rightarrow$ Propagates carry-in directly to carry-out ($C_{i+1} = C_i$).
3. **Kill ($K_i$)**: $\overline{A_i} \cdot \overline{B_i}$ $\rightarrow$ Evaluates `00` as kill, forcing carry-out to zero ($C_{i+1} = 0$).

#### Hierarchical 4-Bit Block Carry Lookahead:
For each 4-bit block $k \in [0, 7]$ spanning bits $[4k \dots 4k+3]$:
* **Group Generate ($G_{blk, k}$)**:
  $$G_{blk, k} = G_{4k+3} \lor (P_{4k+3} \cdot G_{4k+2}) \lor (P_{4k+3} \cdot P_{4k+2} \cdot G_{4k+1}) \lor (P_{4k+3} \cdot P_{4k+2} \cdot P_{4k+1} \cdot G_{4k})$$
* **Group Propagate ($P_{blk, k}$)**:
  $$P_{blk, k} = P_{4k+3} \cdot P_{4k+2} \cdot P_{4k+1} \cdot P_{4k}$$
* **Group Kill ($K_{blk, k}$)**:
  $$K_{blk, k} = \overline{G_{blk, k} \lor P_{blk, k}}$$

#### Parallel Carry Network:
* Inter-block carries $C_{blk}[0 \dots 8]$ are computed simultaneously at the second hierarchical level:
  $$C_{blk}[k+1] = G_{blk, k} \lor (P_{blk, k} \cdot C_{blk}[k])$$
* Internal bit carries $C_i$ are generated in parallel within each block.
* Sum bits: $S_i = P_i \oplus C_i$.
* **Time Complexity**: Reduced from $\mathcal{O}(N)$ (32 delays) to $\mathcal{O}(\log N)$ ($\approx 5$ gate levels).

#### Subtraction & Flag Computation:
* **Subtraction ($A - B$)**: Configured as $A + (\sim B) + 1$ with $C_{in} = 1$.
* **Carry-Out Flag ($C$)**: $C_{32} = C_{blk}[8]$.
* **Signed Overflow Flag ($V$)**: $V = C_{in31} \oplus C_{out31} = C_{31} \oplus C_{32}$.

---

## 3. Double-Precision Newton-Raphson Integer Division & Modulo

### Background & Motivation
Traditional integer division in hardware (Restoring/Non-Restoring division) requires 32 sequential subtract-and-shift cycles. 

### Algorithm & Mathematical Theory
Instead of slow bit-by-bit division, the quotient is evaluated by finding the high-precision reciprocal of divisor $|B|$ using the **Newton-Raphson Reciprocal Iteration**:

#### Reciprocal Iteration:
To find root of $f(X) = \frac{1}{X} - D = 0$:
$$X_{k+1} = X_k \cdot (2 - D \cdot X_k)$$

* **Quadratic Convergence**: Every iteration doubles the number of accurate precision bits ($1 \rightarrow 2 \rightarrow 4 \rightarrow 8 \rightarrow 16 \rightarrow 32 \rightarrow 53$).
* **Initial Seed ($X_0$)**: Extracted using normalized divisor mantissa $D = \text{mant} \cdot 2^{exp}$ ($\text{mant} \in [0.5, 1.0)$):
  $$X_0 = \left( \frac{48}{17} - \frac{32}{17} \cdot \text{mant} \right) \cdot 2^{-exp}$$
* **5 Double-Precision Iterations**: Fully converge to 53-bit floating-point accuracy ($< 10^{-15}$ error).

#### Quotient & Modulo Extraction:
1. **Integer Quotient ($Q$)**:
   $$Q_{mag} = \text{trunc}(|A| \cdot X_{final})$$
2. **Exact Single-Cycle Hardware Boundary Verification**:
   $$\text{while } (Q_{mag} + 1) \cdot |B| \le |A| \implies Q_{mag} \leftarrow Q_{mag} + 1$$
   $$\text{while } Q_{mag} \cdot |B| > |A| \implies Q_{mag} \leftarrow Q_{mag} - 1$$
3. **Exact Hardware Modulo ($R$)**:
   Computed in 1 cycle using the identity:
   $$R = A - (Q \cdot B)$$

---

## 4. IEEE-754 Single-Precision Floating-Point Multiplier (Figure 8.18)

### Background & Architecture
Follows the hardware specification flowchart (Figure 8.18) for IEEE-754 single-precision floating-point multiplication:

```
                  +-----------------------+
                  | Check A == 0 | B == 0 | ---> Yes ---> Return signed 0
                  +-----------------------+
                              | No
                              v
                  +-----------------------+
                  |  S = sign(A) ^ sign(B)|
                  |  E = EA + EB - 127    |
                  +-----------------------+
                              |
                              v
                  +-----------------------+
                  | Early Exponent Check  | ---> Overflow / Underflow Report
                  +-----------------------+
                              |
                              v
                  +-----------------------+
                  | Significand Multiply: |
                  | W = 1.MA * 1.MB (48b) |
                  +-----------------------+
                              |
                              v
                  +-----------------------+
                  | Normalise W & Upd E   | (If bit 47 is 1: W >>= 1, E++)
                  +-----------------------+
                              |
                              v
                  +-----------------------+
                  | Round W (RNE)         | (Guard, Round, Sticky bits)
                  +-----------------------+
                              |
                              v
                  +-----------------------+
                  | Renormalise if needed |
                  +-----------------------+
                              |
                              v
                  +-----------------------+
                  | Construct Float C     |
                  +-----------------------+
```

### Key Stages:
1. **Sign Logic**: $S = \text{sign}(A) \oplus \text{sign}(B)$.
2. **Biased Exponent**: $E = E_A + E_B - 127$.
3. **Significand Product**: 24-bit fixed-point multiplication $(1.M_A \times 1.M_B) \rightarrow$ 48-bit product $W$.
4. **Hardware Normalization**: If $W \ge 2.0$ (bit 47 set), $W \leftarrow W \gg 1$ and $E \leftarrow E + 1$.
5. **Round-to-Nearest-Even (RNE)**:
   * Guard bit: Bit 22
   * Round bit: Bit 21
   * Sticky bit: Logical OR of bits $[20 \dots 0]$
   * Increment mantissa if $\text{Guard} \land (\text{Round} \lor \text{Sticky} \lor \text{LSB})$.
6. **Exception Traps**: Detects and reports `FP_OVF` ($E > 254$) and `FP_UNF` ($E < 1$).

---

## 5. IEEE-754 Single-Precision Floating-Point Divider via Newton-Raphson

### Background & Mathematical Theory
Avoids sequential restoring division of 24-bit mantissas by finding the reciprocal of the normalized divisor mantissa $P_B = 1.M_B \in [1.0, 2.0)$.

#### Algorithmic Stages:
1. **Minimax Initial Seed ($X_0$)**: Optimized for $D \in [1.0, 2.0]$:
   $$X_0 = \frac{24}{17} - \frac{8}{17} \cdot D$$
   Maximum relative error bounded by $|\varepsilon_0| \le 0.059$.
2. **Newton-Raphson Iteration**:
   $$X_{k+1} = X_k \cdot (2.0 - D \cdot X_k)$$
   After 5 iterations, error is $< 10^{-20}$, guaranteeing exact single-precision accuracy.
3. **Mantissa Evaluation**:
   $$W_{fp} = (1.M_A) \cdot X_{final}$$
4. **Exponent Calculation**:
   $$E = E_A - E_B + 127$$
5. **Normalization & Assembly**: Normalized into $[1.0, 2.0)$, rounded with carry-to-exponent propagation, and assembled into $\{S, E[7:0], M[22:0]\}$.

---

## 6. Architectural Flags & Microarchitecture Telemetry Extension

To support advanced exception handling and hardware pipeline telemetry, the processor status register was expanded:

| Flag | Name | Bit Meaning & Trigger Condition |
|:---:|:---|:---|
| **`E`** | Equal | Set if $A == B$ (evaluated during `CMP` or arithmetic). |
| **`GT`** | Greater Than | Set if signed $A >$ signed $B$. |
| **`C`** | Carry / Borrow | Set on unsigned carry-out during addition ($C_{32} = 1$) or no-borrow during subtraction. |
| **`V`** | Signed Overflow | Set if signed 2's complement result exceeds $[-2^{31}, 2^{31}-1]$ ($V = C_{in31} \oplus C_{out31}$). |
| **`N`** | Negative / Sign | Set if MSB of result is 1 ($Result[31] = 1$). |
| **`Z`** | Zero | Set if all 32 bits of result are 0 ($Result == 0$). |
| **`DIV0`** | Divide by Zero | Hardware trap flag triggered when divisor is 0. |
| **`FP_OVF`** | FP Overflow | Triggered when floating-point exponent exceeds $+127$ ($E > 254$). |
| **`FP_UNF`** | FP Underflow | Triggered when floating-point exponent drops below $-126$ ($E < 1$). |
| **`FP_INV`** | FP Invalid Op | Triggered on invalid floating-point operations ($0/0$, $\infty - \infty$). |

---

## 7. ISA Extension: UNARY_REG Format & Extended 24-Opcode Architecture

### Instruction Formats
To accommodate single-source conversions without wasting immediate decoder logic, a 4th instruction format `UNARY_REG` was introduced:

```
UNARY_REG: [Opcode: 31-27 | I=0: 26 | rd: 25-22 | rs1: 21-18 | Unused/0: 17-0]
```

### Expanded Opcodes (24 Total Opcodes):
* `OP_I2D = 0b10101` (21): `i2d rd, rs1` $\rightarrow$ Convert 32-bit Integer in `rs1` to IEEE-754 double bit pattern in `rd`.
* `OP_D2I = 0b10110` (22): `d2i rd, rs1` $\rightarrow$ Convert IEEE-754 double bit pattern in `rs1` to 32-bit Integer in `rd`.
* `OP_FDIV = 0b10111` (23): `fdiv rd, rs1, (rs2/imm)` $\rightarrow$ Fused double Newton-Raphson division.

---

## 8. Hardware Bit-Level Integer <-> Double Conversions via Priority-Encoder CLZ

### Algorithmic Pipeline
1. **Count Leading Zeros (CLZ)**:
   Evaluated using a 5-level parallel binary priority encoder tree in $\mathcal{O}(\log_2 32) = 5$ gate delays.
2. **Double Exponent & Mantissa Alignment**:
   * For non-zero integer $A$, position of leading 1 is $P = 31 - \text{CLZ}(|A|)$.
   * Biased double exponent: $E_{double} = 1023 + P$.
   * Mantissa fraction aligned to 52-bit field: $\text{Fraction} = (|A| \ \& \ \sim(1 \ll P)) \ll (52 - P)$.
3. **Double to Integer Reverse Mapping**:
   * Unbiased exponent $E_{unbiased} = E - 1023$.
   * Mantissa restored with implicit leading 1: $M = (1 \ll 52) \mid \text{Fraction}$.
   * Shifted right by $(52 - E_{unbiased})$ to yield exact 32-bit integer with signed saturation handling.

---

## 9. Architectural Trade-Off & Performance Review

| Modification | Hardware Area Trade-Off | Timing / Cycle Benefit | Evaluation & Recommendation |
|:---|:---|:---|:---|
| **Radix-4 Booth Multiplier** | Adds 3-bit window recoder & multiplexers ($\approx 120$ gates) | Cuts multiplication cycles by **50%** (16 vs 32 iterations) | **Excellent**: Dominant industry standard for RISC cores. |
| **Hierarchical GPK CLA Adder** | Block carry lookahead tree ($\approx 180$ gates) | Decreases critical path from $\mathcal{O}(N)$ to $\mathcal{O}(\log N)$ ($32\Delta \rightarrow 5\Delta$) | **Highly Optimal**: Zero cycle penalties, critical for high-frequency clocking. |
| **Newton-Raphson Fused Division** | Requires double MAC / iterative multiplier block | Quadratic convergence in 5 iterations vs 32-cycle shift-subtract | **Optimal**: Eliminates dedicated multi-stage restoring division arrays. |
| **`UNARY_REG` Instruction Format** | 1 extra decode comparator in silicon instruction decoder | Eliminates false immediate processing and simplifies 2-operand conversion | **Zero Overhead**: Clean ISA extension. |
