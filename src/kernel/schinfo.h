#pragma once

#include <common/list.h>
#include <common/rbtree.h>
struct proc; // dont include proc.h here
struct KernelContext;

// embedded data for cpus
struct sched
{
    // TODO: customize your sched info
    struct proc* idle;
    struct proc* cur_proc;
};

// embeded data for procs
struct schinfo
{
    // TODO: customize your sched info
    //ListNode rq_node;
    u64 vruntime;
    u64 starttime;
    int prio;
    int weight;
    //int slice;
    struct rb_node_ node;
    struct cfs_rq* rq;
};

struct cfs_rq{
    unsigned int weight_sum;
    unsigned int nr_running;
    struct rb_root_ root;
};

// embedded data for containers
struct schqueue
{
    // TODO: customize your sched queue
    
};
