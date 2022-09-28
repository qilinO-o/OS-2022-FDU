#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

/* `exception_vector.S` send all traps here. */
.global trap_entry
trap_entry:
// TODO
    pushp(x29,x30)
    pushp(x27,x28)
    pushp(x25,x26)
    pushp(x23,x24)
    pushp(x21,x22)
    pushp(x19,x20)
    pushp(x17,x18)
    pushp(x15,x16)
    pushp(x13,x14)
    pushp(x11,x12)
    pushp(x9,x10)
    pushp(x7,x8)
    pushp(x5,x6)
    pushp(x3,x4)
    pushp(x1,x2)
    pushp(x0,x1)

    mrs x0, elr_el1
    mrs x1, spsr_el1
    pushp(x0,x1)

    mov x0, sp
    bl trap_global_handler


.global trap_return
trap_return:
// TODO
    popp(x0,x1)
    msr elr_el1, x0
    msr spsr_el1, x1

    pushp(x0,x1)
    pushp(x1,x2)
    pushp(x3,x4)
    pushp(x5,x6)
    pushp(x7,x8)
    pushp(x9,x10)
    pushp(x11,x12)
    pushp(x13,x14)
    pushp(x15,x16)
    pushp(x17,x18)
    pushp(x19,x20)
    pushp(x21,x22)
    pushp(x23,x24)
    pushp(x25,x26)
    pushp(x27,x28)
    pushp(x29,x30)

    eret
    


