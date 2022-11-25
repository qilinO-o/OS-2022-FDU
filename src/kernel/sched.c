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
extern struct container root_container;

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
        if(container_of(lnode,struct schinfo,node)->prio == container_of(rnode,struct schinfo,node)->prio){
            return container_of(lnode,struct schinfo,node) < container_of(rnode,struct schinfo,node);
        }
        return container_of(lnode,struct schinfo,node)->prio < container_of(rnode,struct schinfo,node)->prio;
    }
    return container_of(lnode,struct schinfo,node)->vruntime < container_of(rnode,struct schinfo,node)->vruntime;
}

static SpinLock sched_lock;

void init_schqueue(struct schqueue* sq){
    sq->rq.nr_running = 0;
    sq->rq.weight_sum = 0;
}
define_early_init(sched_lock){
    init_spinlock(&sched_lock);
}

static struct timer proc_timer[NCPU];
static void proc_timer_handle(){
    proc_timer[cpuid()].data = 0;
    setup_checker(tt);
    lock_for_sched(tt);
    sched(tt,RUNNABLE);
}

static const u64 sched_min_granularity = 1;
static const u64 sched_latency = 6;
void init_schinfo(struct schinfo* p, bool group)
{
    // TODO: initialize your customized schinfo for every newly-created process
    p->prio = 21;
    p->weight = prio_to_weight[21];
    p->vruntime = 0;
    p->starttime = 0;
    //p->sq = &(container_of(p, struct proc, schinfo)->container->schqueue);
    p->is_container = group;
}

static u64 get_minvruntime(struct schqueue* sq){
    auto node = _rb_first(&(sq->rq.root));
    if(node == NULL){
        return thisproc()->schinfo.vruntime;
    }
    else{
        return container_of(node,struct schinfo,node)->vruntime;
    }
}
static u64 get_sched_latency(struct schqueue* sq){
    return MAX(sched_latency, sched_min_granularity * sq->rq.nr_running);
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

void _acquire_sched_lock(){
    // TODO acquire the sched_lock if need
    _acquire_spinlock(&sched_lock);
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

bool _activate_proc(struct proc* p, bool onalert)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEING, do nothing if onalert or activate it if else, and return the corresponding value.

    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic
    _acquire_sched_lock();
    if(p->state == RUNNING || p->state == RUNNABLE || (p->state == DEEPSLEEPING && onalert)){
        _release_sched_lock();
        return false;
    }
    else if(p->state == SLEEPING || p->state == UNUSED || (p->state == DEEPSLEEPING && !onalert)){
        struct schqueue *sq = &(p->container->schqueue);
        p->state = RUNNABLE;
        u64 delta = MAX(p->schinfo.vruntime, get_minvruntime(sq)) - p->schinfo.vruntime;
        p->schinfo.vruntime += delta;
        
        ASSERT(_rb_insert(&(p->schinfo.node),&(sq->rq.root),cfs_cmp) == 0);
        sq->rq.weight_sum += p->schinfo.weight;
        sq->rq.nr_running++;
        
        //add originally empty container to its parent's rq, up to root
        struct container* this_cont = p->container;
        while(this_cont != &root_container && this_cont->schqueue.rq.nr_running == 1){
            this_cont->schinfo.vruntime = MAX(this_cont->schinfo.vruntime, get_minvruntime(&(this_cont->parent->schqueue)));
            ASSERT(_rb_insert(&(this_cont->schinfo.node),&(this_cont->parent->schqueue.rq.root),cfs_cmp) == 0);
            this_cont->parent->schqueue.rq.nr_running++;
            this_cont->parent->schqueue.rq.weight_sum += this_cont->schinfo.weight;
            this_cont = this_cont->parent;
        }
    }
    else{ //ZOMBIE
        //PANIC();
        _release_sched_lock();
        return false;
    }
    _release_sched_lock();
    return true;
}

// when activate proc, its container will be add to the father container's schqueue
// therefore, no need to add here
// an empty container wont be in its father container's schqueue in most cases
void activate_group(struct container* group) 
{
    // TODO: add the schinfo node of the group to the schqueue of its parent
    _acquire_sched_lock();
    //struct cfs_rq parent_rq = group->parent->schqueue.rq;
    group->schinfo.vruntime = MAX(group->schinfo.vruntime, get_minvruntime(&(group->parent->schqueue)));
    //ASSERT(_rb_insert(&(group->schinfo.node),&(parent_rq.root),cfs_cmp) == 0);
    _release_sched_lock();
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    struct proc* cp = thisproc();
    cp->state = new_state;
    if(cp->idle){
        cp->schinfo.vruntime += sched_min_granularity;
    }
    else{
        u64 delta = (get_timestamp_ms() - cp->schinfo.starttime) * prio_to_weight[21] / cp->schinfo.weight;
        cp->schinfo.vruntime += delta;
        //modify vruntime up to root
        struct container* this_cont = cp->container;
        while(this_cont != &root_container){
            this_cont->schinfo.vruntime += delta;
            _rb_erase(&(this_cont->schinfo.node),&(this_cont->parent->schqueue.rq.root));
            ASSERT(_rb_insert(&(this_cont->schinfo.node),&(this_cont->parent->schqueue.rq.root),cfs_cmp) == 0);
            this_cont = this_cont->parent;
        }

        struct schqueue* sq = &(cp->container->schqueue);
        if(new_state == RUNNABLE){
            ASSERT(_rb_insert(&(cp->schinfo.node),&(sq->rq.root),cfs_cmp) == 0);
        }
        else{
            sq->rq.weight_sum -= cp->schinfo.weight;
            sq->rq.nr_running--;
            auto cont = cp->container;
            while(cont!=&root_container && cont->schqueue.rq.nr_running == 0){
                _rb_erase(&(cont->schinfo.node),&(cont->parent->schqueue.rq.root));
                cont->parent->schqueue.rq.nr_running--;
                cont->parent->schqueue.rq.weight_sum -= cont->schinfo.weight;
                cont = cont->parent;
            }
        }
    }
}

struct proc* recur_get_proc(struct container* cont){
    auto node = _rb_first(&(cont->schqueue.rq.root));
    if(node == NULL){
        return NULL;
    }
    else{
        struct schinfo* info = container_of(node,struct schinfo, node);
        if(info->is_container){
            struct container* child_cont = container_of(info, struct container, schinfo);
            struct proc* p = recur_get_proc(child_cont);
            return p;
        }
        else{
            struct proc* p = container_of(info, struct proc, schinfo);
            _rb_erase(node,&(cont->schqueue.rq.root));
            return p;
        }
    }
}
static struct proc* pick_next(){
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    if(panic_flag){
        return cpus[cpuid()].sched.idle;
    }
    struct proc* p = recur_get_proc(&root_container);
    if(p != NULL){
        return p;
    }
    else{
        return cpus[cpuid()].sched.idle;
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
    if(p->idle){
        proc_timer[cpuid()].elapse = sched_min_granularity;
    }
    else{
        struct schqueue* sq = &(p->container->schqueue);
        proc_timer[cpuid()].elapse = MAX(sched_min_granularity, get_sched_latency(sq) * p->schinfo.weight / sq->rq.weight_sum);
    }
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

