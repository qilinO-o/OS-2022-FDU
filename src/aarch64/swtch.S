// Do kernel-mode context switch
// x0 (first parameter): new context ptr
// x1 (second parameter): addr to save old context ptr

#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

.globl swtch
swtch:
// TODO
/* Save old callee-saved registers */
    pushp(x17,x18)
    pushp(x19,x20)
    pushp(x21,x22)
    pushp(x23,x24)
    pushp(x25,x26)
    pushp(x27,x28)
    pushp(x29,x30)
    pushp(x0,x1)

/* Switch stacks */
    mov x2, sp
    str x2, [x1]
    mov sp, x0

/* Load new callee-saved registers */
    popp(x0,x1)
    popp(x29,x30)
    popp(x27,x28)
    popp(x25,x26)
    popp(x23,x24)
    popp(x21,x22)
    popp(x19,x20)
    popp(x17,x18)

