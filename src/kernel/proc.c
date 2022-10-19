#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>

#include <common/rbtree.h>
#include <common/string.h>
#include <kernel/printk.h>
#define PID_MAX_DEFAULT 0x8000

struct proc root_proc;

static SpinLock pid_proc_tree_lock;
struct pid_proc_rb_t{
    u64 pid;
    struct proc* proc;
    struct rb_node_ node;
};
struct pid_proc_map_t{
    u64 num;
    struct rb_root_ root;
};
static struct pid_proc_map_t pid_proc_map;
define_early_init(pid_proc_map){
    init_spinlock(&pid_proc_tree_lock);
    pid_proc_map.num = 0;
}
static bool __pid_proc_cmp(rb_node lnode, rb_node rnode){
    return container_of(lnode, struct pid_proc_rb_t, node)->pid < container_of(rnode, struct pid_proc_rb_t, node)->pid;
}
void insert_pid_proc_map(struct proc* p){
    struct pid_proc_rb_t* node = kalloc(sizeof(struct pid_proc_rb_t));
    node->pid = p->pid;
    node->proc = p;
    _acquire_spinlock(&pid_proc_tree_lock);
    _rb_insert(&(node->node), &(pid_proc_map.root), __pid_proc_cmp);
    pid_proc_map.num++;
    _release_spinlock(&pid_proc_tree_lock);
}

void kernel_entry();
void proc_entry();

static SpinLock proc_tree_lock;
define_early_init(proc_tree){
    init_spinlock(&proc_tree_lock);
}

/* var of pid and functions to allocate pid */
static int pid_base = -1;
static SpinLock pid_base_lock;

typedef struct pidmap{
    unsigned int nr_free;
    i8 page[PID_MAX_DEFAULT/8];
} pidmap_t;

static pidmap_t pidmap = {PID_MAX_DEFAULT, {'0'}};

int test_and_set_pid(int offset, void* addr){
    unsigned long mask = 1UL << (offset & (sizeof(unsigned long) * 8 - 1));
    unsigned long* p = ((unsigned long*)addr) + (offset >> (sizeof(unsigned long) + 1));
    unsigned long old = *p;
    *p = old | mask;
    return (old & mask) != 0;
}
void clear_bit(int offset, void* addr){
    unsigned long mask = 1UL << (offset & (sizeof(unsigned long) * 8 - 1));
    unsigned long* p = ((unsigned long*)addr) + (offset >> (sizeof(unsigned long) + 1));
    unsigned long old = *p;
    *p = old & ~mask;
}
int find_next_zero_bit(void* addr, int size, int offset){
    unsigned long *p;
    unsigned long mask;
    while(offset < size){
        mask = 1UL << (offset & (sizeof(unsigned long) * 8 - 1));
        p = ((unsigned long*)addr) + (offset >> (sizeof(unsigned long) + 1));
        if((~(*p) & mask)){
            break;
        }
        ++offset;
    }
    return offset;
}
int get_next_pid(){
    _acquire_spinlock(&pid_base_lock);
    int pid = pid_base + 1;
    int offset = pid & 32767;
    if(pidmap.nr_free == 0){
        _release_spinlock(&pid_base_lock);
        return -1;
    }
    offset = find_next_zero_bit(&pidmap.page, 32768, offset);
    if(offset == 32768) offset = find_next_zero_bit(&pidmap.page, offset-1, 0);
    if((offset != 32768) && (!test_and_set_pid(offset, &pidmap.page))){
        --pidmap.nr_free;
        pid_base = offset;
        _release_spinlock(&pid_base_lock);
        return offset;
    }
    _release_spinlock(&pid_base_lock);
    return -1;
}
void free_pid(int pid){
    _acquire_spinlock(&pid_base_lock);
    int offset = pid & 32767;
    ++pidmap.nr_free;
    clear_bit(offset, &pidmap.page);
    _release_spinlock(&pid_base_lock);
}

define_early_init(pid_base){
    init_spinlock(&pid_base_lock);
}
// int get_next_pid(){
//     setup_checker(ch_pid_base_lock);
//     acquire_spinlock(ch_pid_base_lock, &pid_base_lock);
//     pid_base += 1;
//     release_spinlock(ch_pid_base_lock, &pid_base_lock);
//     return pid_base;
// }

/* pid section enc */


void set_parent_to_this(struct proc* proc){
    // TODO set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    ASSERT(proc->parent == NULL);

    setup_checker(ch_proc_tree_lock);
    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    struct proc* cp = thisproc();
    proc->parent = cp;
    _insert_into_list(&(cp->children), &(proc->ptnode));
    release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
}

NO_RETURN void exit(int code){
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    //printk("%d in exit\n",thisproc()->pid);
    setup_checker(ch_proc_tree_lock);
    struct proc* cp = thisproc();
    ASSERT(cp != &root_proc);
    cp->exitcode = code;
    free_pgdir(&(cp->pgdir));

    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    ListNode* child_list = &(cp->children);
    if(!_empty_list(child_list)){
        for(struct ListNode* node = child_list->next;;){
            struct proc* child_proc = container_of(node, struct proc, ptnode);
            ASSERT(child_proc->parent == cp);
            child_proc->parent = &root_proc;
            node = _detach_from_list(node);
            _insert_into_list(&(root_proc.children), &(child_proc->ptnode));
            if(is_zombie(child_proc)){
                //TODO:
                //activate_proc(&root_proc);
                post_sem(&(root_proc.childexit));
            }
            if(node->next == child_list){
                break;
            }
            else{
                node = node->next;
            }
        }
    }
    ASSERT(_empty_list(child_list));
    
    post_sem(&(cp->parent->childexit)); //sequence is important! not sure
        //printk("%d:%d ",cpuid(),thisproc()->pid);
    
    setup_checker(ch_sched);
    lock_for_sched(ch_sched);
    release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    sched(ch_sched,ZOMBIE);
    

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode){
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    //printk("proc %d in wait\n",thisproc()->pid);
    setup_checker(ch_proc_tree_lock);
    struct proc* cp = thisproc();
    ListNode* child_list = &(cp->children);
    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    if(_empty_list(child_list)){
        release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
        return -1;
    }
    release_spinlock(ch_proc_tree_lock, &proc_tree_lock);

    wait_sem(&(cp->childexit));
    
    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    _for_in_list(node, child_list){
        struct proc* child_proc = container_of(node, struct proc, ptnode);
        if(is_zombie(child_proc)){
            ASSERT(child_proc->parent == cp);
            _detach_from_list(node);
            child_proc->state = UNUSED;
            int pid_ret = child_proc->pid;
            *exitcode = child_proc->exitcode;
            
            _acquire_spinlock(&pid_proc_tree_lock);
            struct pid_proc_rb_t dest = {child_proc->pid,NULL,{NULL,NULL,0}};
            auto del_node = _rb_lookup(&(dest.node),&(pid_proc_map.root),__pid_proc_cmp);
            ASSERT(del_node!=NULL);
            _rb_erase(del_node, &(pid_proc_map.root));
            pid_proc_map.num--;
            _release_spinlock(&pid_proc_tree_lock);
            
            free_pid(child_proc->pid);
            kfree_page(child_proc->kstack);
            kfree((void*)child_proc);
            release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
            return pid_ret;
        }
    }
    PANIC();
    release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    return -1;
}

int kill(int pid){
    // TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    struct pid_proc_rb_t dest = {pid,NULL,{NULL,NULL,0}};
    _acquire_spinlock(&proc_tree_lock);
    _acquire_spinlock(&pid_proc_tree_lock);
    auto kill_node = _rb_lookup(&(dest.node),&(pid_proc_map.root),__pid_proc_cmp);
    _release_spinlock(&pid_proc_tree_lock);
    if(kill_node == NULL){
        return -1;
    }
    struct proc* p = container_of(kill_node, struct pid_proc_rb_t, node)->proc;
    if(p->state == UNUSED){
        return -1;
    }
    p->killed = true;
    activate_proc(p);
    _release_spinlock(&proc_tree_lock);
    return 0;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    setup_checker(ch_proc_tree_lock);
    //printk("in start_proc\n");
    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    if(p->parent == NULL){
        p->parent = &root_proc;
        _insert_into_list(&(root_proc.children), &(p->ptnode));
    }
    release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
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
    insert_pid_proc_map(p);
    p->exitcode = 0;
    p->state = UNUSED;
    init_sem(&(p->childexit),0);
    init_list_node(&(p->children));
    init_list_node(&(p->ptnode));
    p->parent = NULL;
    init_schinfo(&(p->schinfo));
    init_pgdir(&(p->pgdir));
    p->kstack = kalloc_page();
    ASSERT(p->kstack != NULL);
    p->ucontext = (UserContext*)(p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));
    p->kcontext = (KernelContext*)(p->kstack + PAGE_SIZE - 16 - sizeof(UserContext) - sizeof(KernelContext));
}

struct proc* create_proc(){
    //printk("in_creat_proc\n");
    struct proc* p = kalloc(sizeof(struct proc));
    //printk("finish_kalloc_for_proc\n");
    init_proc(p);
    //printk("finish_init_proc\n");
    return p;
}

define_init(root_proc){
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}
