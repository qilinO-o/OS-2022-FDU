#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

struct proc root_proc;

void kernel_entry();
void proc_entry();

static SleepLock* proc_tree_lock;
define_early_init(proc_tree){
    init_sleeplock(proc_tree_lock);
}

/* var of pid and functions to allocate pid */
static int pid_base = 0;
static SleepLock* pid_base_lock;
define_early_init(pid_base){
    init_sleeplock(pid_base_lock);
    pid_base = 0;
}
int get_next_pid(){
    setup_checker(ch_pid_base_lock);
    acquire_sleeplock(ch_pid_base_lock, pid_base_lock);
    pid_base += 1;
    release_sleeplock(ch_pid_base_lock, pid_base_lock);
    return pid_base - 1;
}

void set_parent_to_this(struct proc* proc){
    // TODO set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    setup_checker(ch_proc_tree_lock);
    acquire_sleeplock(ch_proc_tree_lock, proc_tree_lock);
    proc->parent = thisproc();
    _insert_into_list(&(thisproc()->children), &(proc->ptnode));
    release_sleeplock(ch_proc_tree_lock, proc_tree_lock);
}

NO_RETURN void exit(int code){
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources, say files etc.
    // 3. post parent->childexit
    // 4. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    setup_checker(ch_proc_tree_lock);
    struct proc* cp = thisproc();
    ASSERT(cp != &root_proc);
    cp->exitcode = code;
    kfree_page(cp->kstack);
    

    acquire_sleeplock(ch_proc_tree_lock, proc_tree_lock);
    ListNode* child_list = &(cp->children);
    _for_in_list(node, child_list){
        struct proc* child_proc = container_of(node, struct proc, children);
        ASSERT(child_proc->parent == cp);
        child_proc->parent = &root_proc;
        _detach_from_list(node);
        _insert_into_list(&(root_proc.children), node);
        if(is_zombie(child_proc)){
            //TODO:
            activate_proc(&root_proc);
        }
    }
    ASSERT(_empty_list(child_list));
    release_sleeplock(ch_proc_tree_lock, proc_tree_lock);
    
    setup_checker(ch_sched);
    lock_for_sched(ch_sched);
    sched(ch_sched,ZOMBIE);

    post_sem(&(cp->parent->childexit)); //sequence is important! not sure

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode){
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    setup_checker(ch_proc_tree_lock);
    struct proc* cp = thisproc();
    ListNode* child_list = &(cp->children);
    acquire_sleeplock(ch_proc_tree_lock, proc_tree_lock);
    if(_empty_list(child_list)){
        return -1;
    }
    wait_sem(&(cp->childexit));
    _for_in_list(node, child_list){
        struct proc* child_proc = container_of(node, struct proc, ptnode);
        if(is_zombie(child_proc)){
            ASSERT(child_proc->parent == cp);
            _detach_from_list(node);
            child_proc->state = UNUSED;
            int pid_ret = child_proc->pid;
            *exitcode = child_proc->exitcode;
            kfree((void*)child_proc);
            release_sleeplock(ch_proc_tree_lock, proc_tree_lock);
            return pid_ret;
        }
    }
    ASSERT(1);
    release_sleeplock(ch_proc_tree_lock, proc_tree_lock);
    return -1;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg){
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    setup_checker(ch_proc_tree_lock);
    struct proc* cp = thisproc();
    
    if(p->parent == NULL){
        acquire_sleeplock(ch_proc_tree_lock, proc_tree_lock);
        p->parent = &root_proc;
        _insert_into_list(&(root_proc.children), &(p->ptnode));
        release_sleeplock(ch_proc_tree_lock, proc_tree_lock);
    }
    
    //TODO
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;

    int pid_ret = p->pid;
    activate_proc(p);
    return pid_ret;
}

void init_proc(struct proc* p){
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    p->killed = false;
    p->idle = false;
    p->pid = get_next_pid();
    p->exitcode = 0;
    p->state = UNUSED;
    init_sem(&(p->childexit),0);
    init_list_node(&(p->children));
    init_list_node(&(p->ptnode));
    p->parent = NULL;
    init_schinfo(&(p->schinfo));
    p->kstack = kalloc_page();
    p->ucontext = (UserContext*)(p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));
    p->kcontext = (KernelContext*)(p->kstack + PAGE_SIZE - 16 - sizeof(UserContext) - sizeof(KernelContext));
}

struct proc* create_proc(){
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

define_init(root_proc){
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}
