#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/sd.h>
#include <kernel/paging.h>

bool panic_flag;
extern char icode[];
extern char eicode[];

NO_RETURN void idle_entry() {
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

void trap_return();
void kernel_entry() {
    printk("hello world %d\n", (int)sizeof(struct proc));

    // proc_test();
    // user_proc_test();
    // container_test();
    // sd_test();
    
    do_rest_init();
    // pgfault_first_test();
    // pgfault_second_test();

    // TODO: map init.S to user space and trap_return to run icode
    struct proc* p = thisproc();
    printk("init on pid=%d\n",p->pid);
    p->cwd = inodes.root;
    struct section *st = kalloc(sizeof(struct section));
    st->begin = (u64)icode - PAGE_BASE((u64)icode);
    st->end = st->begin + (u64)eicode-(u64)icode;
    st->flags = ST_TEXT;
    init_sleeplock(&(st->sleeplock));
    _insert_into_list(&(p->pgdir.section_head), &(st->stnode));
    for(u64 va = PAGE_BASE((u64)icode);va <= (u64)eicode; va += PAGE_SIZE){
        vmmap(&(p->pgdir), 0x0, (void*)va, PTE_USER_DATA | PTE_RO);
    }
    
    p->ucontext->elr = (u64)icode - PAGE_BASE((u64)icode);
    set_return_addr(trap_return);
}

NO_INLINE NO_RETURN void _panic(const char* file, int line) {
    printk("=====%s:%d PANIC%d!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}
