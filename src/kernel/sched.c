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

static const int prio_to_weight[40]={
/* -20 */ 88761, 71755, 56483, 46273, 36291,
/* -15 */ 29154, 23254, 18705, 14949, 11916,
/* -10 */ 9548, 7620, 6100, 4904, 3906,
/* -5 */ 3121, 2501, 1991, 1586, 1277,
/* 0 */ 1024, 820, 655, 526, 423,
/* 5 */ 335, 272, 215, 172, 137,
/* 10 */ 110, 87, 70, 56, 45,
/* 15 */ 36, 29, 23, 18, 15
};
bool cfs_cmp(rb_node lnode,rb_node rnode){
    if(container_of(lnode,struct schinfo,node)->vruntime == container_of(rnode,struct schinfo,node)->vruntime){
        return container_of(lnode,struct proc,schinfo.node)->pid < container_of(rnode,struct proc,schinfo.node)->pid;
    }
    return container_of(lnode,struct schinfo,node)->vruntime < container_of(rnode,struct schinfo,node)->vruntime;
}

static SpinLock sched_lock;
static struct cfs_rq rq;

static struct timer proc_timer[NCPU];
static void proc_timer_handle(){
    proc_timer[cpuid()].data = 0;
    setup_checker(tt);
    lock_for_sched(tt);
    sched(tt,RUNNABLE);
}

static const u64 sched_min_granularity = 1;
static const u64 sched_latency = 6;

static u64 get_minvruntime(){
    auto node = _rb_first(&(rq.root));
    if(node == NULL){
        return thisproc()->schinfo.vruntime;
    }
    else{
        return container_of(node,struct schinfo,node)->vruntime;
    }
}
static u64 get_sched_latency(){
    return MAX(sched_latency, sched_min_granularity * rq.nr_running);
}

define_early_init(sched_rq){
    init_spinlock(&sched_lock);
    rq.nr_running = 1;
    rq.weight_sum = 15;
}

define_init(scheduler){
    for(int i=0;i<NCPU;++i){
        struct proc* new_idle = kalloc(sizeof(struct proc));
        new_idle->killed = 0;
        new_idle->pid = -1;
        new_idle->idle = true;
        new_idle->state = RUNNING;
        new_idle->schinfo.vruntime = 0;
        new_idle->schinfo.prio = 39;
        new_idle->schinfo.weight = prio_to_weight[39];
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
    p->prio = 21;
    p->weight = prio_to_weight[21];
    p->vruntime = 0;
    p->starttime = 0;
    p->rq = &rq;
}

void _acquire_sched_lock(){
    // TODO acquire the sched_lock if need
    //printk("ac1\n");
    _acquire_spinlock(&sched_lock);
    //printk("ac2\n");
}

void _release_sched_lock(){
    // TODO release the sched_lock if need
    _release_spinlock(&sched_lock);
}

bool is_zombie(struct proc* p){
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

bool activate_proc(struct proc* p)
{
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
        p->schinfo.vruntime = MAX(p->schinfo.vruntime,get_minvruntime());
        _rb_insert(&(p->schinfo.node),&(rq.root),cfs_cmp);
        rq.weight_sum += p->schinfo.weight;
        rq.nr_running++;
    }
    else{ //ZOMBIE
        //PANIC();
        _release_sched_lock();
        return false;
    }
    _release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state){
    // TODO: if using simple_sched, you should implement this routine
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    struct proc* cp = thisproc();
    cp->state = new_state;
    cp->schinfo.vruntime += (get_timestamp_ms() - cp->schinfo.starttime) * prio_to_weight[21] / cp->schinfo.weight;
    if(new_state == RUNNABLE && !cp->idle){
        _rb_insert(&(cp->schinfo.node),&(rq.root),cfs_cmp);
    }
    else if(!cp->idle){
        rq.weight_sum -= cp->schinfo.weight;
        rq.nr_running--;
    }
}

static struct proc* pick_next(){
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    if(panic_flag){
        return cpus[cpuid()].sched.idle;
    }
    auto node = _rb_first(&(rq.root));
    if(node != NULL){
        struct schinfo* info = container_of(node,struct schinfo, node);
        struct proc* p = container_of(info, struct proc, schinfo);
        //printk("~%d in %d\n\n",p->pid,p->state);
        _rb_erase(node,&(rq.root));
        return p;
    }
    else{
        //printk("~+%d in %d\n",thisproc()->pid,thisproc()->state);
        if(thisproc()->state == RUNNABLE) return thisproc();
        else return cpus[cpuid()].sched.idle;
    }
}

static void update_this_proc(struct proc* p){
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    //reset_clock(1000);
    cpus[cpuid()].sched.cur_proc = p;
    if(proc_timer[cpuid()].data){
        cancel_cpu_timer(&proc_timer[cpuid()]);
        proc_timer[cpuid()].data = 0;
    }
    proc_timer[cpuid()].elapse = MAX(sched_min_granularity, get_sched_latency() * p->schinfo.weight / rq.weight_sum);
    // if(p->idle){
    //     proc_timer[cpuid()].elapse = 1;
    // }
    proc_timer[cpuid()].handler = proc_timer_handle;
    set_cpu_timer(&proc_timer[cpuid()]);
    proc_timer[cpuid()].data = 1;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state){
    auto this = thisproc();
    if(this->killed && new_state!=ZOMBIE){
        _release_sched_lock();
        return;
    }
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    //if(next->idle) printk("%d",cpuid());
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    next->schinfo.starttime = get_timestamp_ms();
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

