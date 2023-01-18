#include <common/string.h>
#include <common/list.h>
#include <kernel/container.h>
#include <kernel/init.h>
#include <kernel/printk.h>
#include <kernel/mem.h>
#include <kernel/sched.h>

struct container root_container;
extern struct proc root_proc;

/************ var of pid and functions to allocate pid ************/
// static for root container
static pidmap_t pidmap;
static BitmapCell lpage[LOCAL_PID_MAX_DEFAULT/8];

/************ var of pid and functions to allocate pid ************/

void activate_group(struct container* group);

void set_container_to_this(struct proc* proc)
{
    proc->container = thisproc()->container;
}

void init_container(struct container* container)
{
    memset(container, 0, sizeof(struct container));
    container->parent = NULL;
    container->rootproc = NULL;
    init_schinfo(&container->schinfo, true);
    init_schqueue(&container->schqueue);
    // TODO: initialize namespace (local pid allocator)
    if(container != &root_container){
        init_pidmap(&(container->pidmap), LOCAL_PID_MAX_DEFAULT);
    }
    else{
        pidmap.pid_namespace_size = LOCAL_PID_MAX_DEFAULT;
        pidmap.nr_free = LOCAL_PID_MAX_DEFAULT;
        pidmap.pid_base = 0;
        init_spinlock(&pidmap.pid_base_lock);
        memset(lpage,0,sizeof(lpage));
        pidmap.page = lpage;
        container->pidmap = pidmap;
    }
}

struct container* create_container(void (*root_entry)(), u64 arg)
{
    // TODO
    struct container* new_container = kalloc(sizeof(struct container));
    init_container(new_container);
    new_container->parent = thisproc()->container;
    struct proc* new_root_proc = create_proc();
    new_container->rootproc = new_root_proc;
    set_parent_to_this(new_root_proc);
    new_root_proc->container = new_container;
    start_proc(new_root_proc,root_entry,arg);
    activate_group(new_container);
    return new_container;
}

define_early_init(root_container)
{
    init_container(&root_container);
    root_container.rootproc = &root_proc;
}
