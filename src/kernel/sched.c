#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>

extern bool panic_flag;

extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock* sched_lock;
static ListNode rq;

define_early_init(sched_rq){
    init_spinlock(sched_lock);
    init_list_node(&rq);
}

define_init(scheduler){
    for(int i=0;i<NCPU;++i){
        struct proc* new_idle = kalloc(sizeof(struct proc));
        new_idle->pid = -1;
        new_idle->idle = true;
        new_idle->state = RUNNING;
        cpus[i].sched.idle = new_idle;
        cpus[i].sched.cur_proc = new_idle;
    }
}

struct proc* thisproc(){
    // TODO return the current process
    return cpus[cpuid()].sched.cur_proc;
}

void init_schinfo(struct schinfo* p){
    // TODO initialize your customized schinfo for every newly-created process
    init_list_node(&(p->rq_node));
}

void _acquire_sched_lock(){
    // TODO acquire the sched_lock if need
    _acquire_spinlock(sched_lock);
}

void _release_sched_lock(){
    // TODO release the sched_lock if need
    _release_spinlock(sched_lock);
}

bool is_zombie(struct proc* p){
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool activate_proc(struct proc* p){
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic
    _acquire_sched_lock();
    if(p->state == RUNNING || p->state == RUNNABLE){
        _release_sched_lock();
        return false;
    }
    else if(p->state == SLEEPING || p->state == UNUSED){
        p->state = RUNNABLE;
        _insert_into_list(&rq, &(p->schinfo.rq_node));
    }
    else{
        PANIC();
    }
    _release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state){
    // TODO: if using simple_sched, you should implement this routine
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    struct proc* cp = thisproc();
    cp->state = new_state;
    if(new_state == SLEEPING || new_state == ZOMBIE){
        _detach_from_list(&(cp->schinfo.rq_node));
    }
}

static struct proc* pick_next(){
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    if(panic_flag){
        return cpus[cpuid()].sched.idle;
    }
    _for_in_list(node, &rq){
        if(node == &rq){
            continue;
        }
        struct proc* next_proc = container_of(node, struct proc, schinfo.rq_node);
        if(next_proc->state == RUNNABLE){
            return next_proc;
        }
    }
    return cpus[cpuid()].sched.idle;
}

static void update_this_proc(struct proc* p){
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    reset_clock(1000);
    cpus[cpuid()].sched.cur_proc = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state){
        printk("1");
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this){
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg){
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}

