; ========================================================
; SimpleRISC Evaluation 1 Assembly Demo
; ========================================================

.text 0x0000
    mov r0, 5           ; r0 is general-purpose (not hardwired)
    mov r1, 10          ; r1 = 10
    mov r2, 20          ; r2 = 20
    add r3, r1, r2      ; r3 = 10 + 20 = 30
    mul r4, r1, r2      ; r4 = 10 * 20 = 200
    cmp r1, 10          ; Compare r1 with 10 -> flags.E = 1
    beq is_equal
    mov r5, 999         ; Skipped
    b finish

is_equal:
    mov r5, 42          ; r5 = 42

finish:
    nop
