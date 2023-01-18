#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>

#include <common/rbtree.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/paging.h>

struct proc root_proc;
extern struct container root_container;

static SpinLock pid_proc_tree_lock;

/***************** pid - proc map ********************/
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
static void insert_pid_proc_map(struct proc* p){
    struct pid_proc_rb_t* node = kalloc(sizeof(struct pid_proc_rb_t));
    node->pid = p->pid;
    node->proc = p;
    _acquire_spinlock(&pid_proc_tree_lock);
    ASSERT(_rb_insert(&(node->node), &(pid_proc_map.root), __pid_proc_cmp) == 0);
    pid_proc_map.num++;
    _release_spinlock(&pid_proc_tree_lock);
}
/***************** pid - proc map ********************/

void kernel_entry();
void proc_entry();

static SpinLock proc_tree_lock;
define_early_init(proc_tree){
    init_spinlock(&proc_tree_lock);
}

/************ var of pid and functions to allocate pid ************/
static pidmap_t global_pidmap;
static BitmapCell page[PID_MAX_DEFAULT/8];

define_early_init(global_pidmap){
    global_pidmap.pid_namespace_size = PID_MAX_DEFAULT;
    global_pidmap.nr_free = PID_MAX_DEFAULT;
    global_pidmap.pid_base = -1;
    init_spinlock(&global_pidmap.pid_base_lock);
    memset(page,0,sizeof(page));
    global_pidmap.page = page;
}
/************ var of pid and functions to allocate pid ************/


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
    // 3. transfer children to the rootproc of the container, and notify the it if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    setup_checker(ch_proc_tree_lock);
    struct proc* cp = thisproc();
    ASSERT(cp != cp->container->rootproc && !cp->idle);
    cp->exitcode = code;
    // release files used by this proc
    for(int i = 0; i < NOFILE; ++i) {
        if(cp->oftable.files_p[i] != NULL){
            fileclose(cp->oftable.files_p[i]);
            cp->oftable.files_p[i] = NULL;
        }
    }
    // release current working dictionary
    OpContext ctx;
    bcache.begin_op(&ctx);
    inodes.put(&ctx, cp->cwd);
    bcache.end_op(&ctx);
    cp->cwd = NULL;

    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    ListNode* child_list = &(cp->children);
    if(!_empty_list(child_list)){
        for(struct ListNode* node = child_list->next;;){
            struct proc* child_proc = container_of(node, struct proc, ptnode);
            ASSERT(child_proc->parent == cp);
            child_proc->parent = cp->container->rootproc;
            node = _detach_from_list(node);
            _insert_into_list(&(cp->container->rootproc->children), &(child_proc->ptnode));
            if(is_zombie(child_proc)){
                //TODO:
                //activate_proc(&root_proc);
                post_sem(&(cp->container->rootproc->childexit));
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
    
    setup_checker(ch_sched);
    lock_for_sched(ch_sched);
    release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    sched(ch_sched,ZOMBIE);
    

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode, int* pid)
{
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its local pid and exitcode
    // NOTE: be careful of concurrency
    setup_checker(ch_proc_tree_lock);
    struct proc* cp = thisproc();
    ListNode* child_list = &(cp->children);
    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    if(_empty_list(child_list)){
        release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
        return -1;
    }
    release_spinlock(ch_proc_tree_lock, &proc_tree_lock);

    bool wait_sig = wait_sem(&(cp->childexit));
    if(wait_sig == false){
        return -1;
    }
    //printk("wake up, pid=%d\n",thisproc()->pid);
    
    acquire_spinlock(ch_proc_tree_lock, &proc_tree_lock);
    _for_in_list(node, child_list){
        if(node == child_list) break;
        struct proc* child_proc = container_of(node, struct proc, ptnode);
        if(is_zombie(child_proc)){
            ASSERT(child_proc->parent == cp);
            //printk("pid=%d is exit\n", child_proc->pid);
            // delete it from current proc's child list
            _detach_from_list(node);
            child_proc->state = UNUSED;
            int pid_global = child_proc->pid;
            int pid_local = child_proc->localpid;
            *exitcode = child_proc->exitcode;
            
            // delete it from the pid-proc map
            _acquire_spinlock(&pid_proc_tree_lock);
            struct pid_proc_rb_t dest = {child_proc->pid,NULL,{NULL,NULL,0}};
            auto del_node = _rb_lookup(&(dest.node),&(pid_proc_map.root),__pid_proc_cmp);
            ASSERT(del_node!=NULL);
            _rb_erase(del_node, &(pid_proc_map.root));
            pid_proc_map.num--;
            _release_spinlock(&pid_proc_tree_lock);
            
            // free resources of the child proc  
            free_pgdir(&(child_proc->pgdir));
            free_pid(&global_pidmap, child_proc->pid);
            free_pid(&(child_proc->container->pidmap), child_proc->localpid);
            kfree_page(child_proc->kstack);
            kfree((void*)child_proc);
            release_spinlock(ch_proc_tree_lock, &proc_tree_lock);
            *pid = pid_global;
            return pid_local;
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
        _release_spinlock(&proc_tree_lock);
        return -1;
    }
    struct proc* p = container_of(kill_node, struct pid_proc_rb_t, node)->proc;
    if(p->state == UNUSED){
        _release_spinlock(&proc_tree_lock);
        return -1;
    }
    p->killed = true;
    p->schinfo.prio = 0;
    p->schinfo.weight = 88761;
    alert_proc(p);
    _release_spinlock(&proc_tree_lock);
    return 0;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its local pid
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

    p->localpid = get_next_pid(&(p->container->pidmap));
    int pid_ret = p->localpid;
    activate_proc(p);
    return pid_ret;
}

void init_proc(struct proc* p){
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    p->killed = false;
    p->idle = false;
    p->pid = get_next_pid(&global_pidmap);
    insert_pid_proc_map(p);
    p->exitcode = 0;
    p->state = UNUSED;
    init_sem(&(p->childexit),0);
    init_list_node(&(p->children));
    init_list_node(&(p->ptnode));
    p->parent = NULL;
    init_schinfo(&(p->schinfo), false);
    init_pgdir(&(p->pgdir));
    p->kstack = kalloc_page();
    memset(p->kstack,0,PAGE_SIZE);
    ASSERT(p->kstack != NULL);
    p->ucontext = (UserContext*)(p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));
    p->kcontext = (KernelContext*)(p->kstack + PAGE_SIZE - 16 - sizeof(UserContext) - sizeof(KernelContext));
    p->container = &root_container;
    init_oftable(&(p->oftable));
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

struct proc* _get_offline_proc(struct proc* this_proc){
    _acquire_spinlock(&(this_proc->pgdir.lock));
    if(!(this_proc->pgdir.online)){
        return this_proc;
    }
    else{
        _release_spinlock(&(this_proc->pgdir.lock));
        _for_in_list(node, &(this_proc->children)){
            if(node == &(this_proc->children)) break;
            struct proc* child = container_of(node, struct proc, ptnode);
            struct proc* ret = _get_offline_proc(child);
            if(ret != NULL){
                return ret;
            }
        }
        return NULL;
    }
}
struct proc* get_offline_proc(){
    return _get_offline_proc(&root_proc);
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork() {
    /* TODO: Your code here. */
    struct proc* child_proc = create_proc();
    struct proc* this_proc = thisproc();
    set_parent_to_this(child_proc);
    // copy all registers
    memmove(child_proc->ucontext, this_proc->ucontext, sizeof(UserContext));
    // fork return as child
    child_proc->ucontext->reserved[0] = 0;
    // use same working dictionary
    child_proc->cwd = inodes.share(this_proc->cwd);
    // copy oftable to use same files
    for(int i=0;i<NOFILE;++i){
        if(this_proc->oftable.files_p[i] != NULL){
            child_proc->oftable.files_p[i] = filedup(this_proc->oftable.files_p[i]);
        }
    }
    // copy pgdir
    PTEntriesPtr old_pte;
    _for_in_list(node, &(this_proc->pgdir.section_head)){
        if(node == &(this_proc->pgdir.section_head)){
            break;
        }
        struct section* st = container_of(node, struct section, stnode);
        // fork with cow
        for(u64 va = PAGE_BASE(st->begin); va < st->end; va += PAGE_SIZE){
            old_pte = get_pte(&(this_proc->pgdir), va, false);
            if((old_pte == NULL) || !(*old_pte & PTE_VALID)){
                continue;
            }
            vmmap(&(child_proc->pgdir), va, (void*)P2K(PTE_ADDRESS(*old_pte))
            , PTE_FLAGS(*old_pte) | PTE_RO);
        }
        // fork without cow
        // for(u64 va = PAGE_BASE(st->begin); va < st->end; va += PAGE_SIZE){
        //     old_pte = get_pte(&(this_proc->pgdir), va, false);
        //     if((old_pte == NULL) || !(*old_pte & PTE_VALID)){
        //         continue;
        //     }
        //     void* new_page = kalloc_page();
        //     vmmap(&(child_proc->pgdir), va, new_page, PTE_FLAGS(*old_pte));
        //     copyout(&(child_proc->pgdir), (void*)va, (void*)P2K(PTE_ADDRESS(*old_pte)), PAGE_SIZE);
        // }
    }
    copy_sections(&(this_proc->pgdir.section_head), &(child_proc->pgdir.section_head));

    // start proc
    start_proc(child_proc, trap_return, 0);
    return child_proc->localpid;
}