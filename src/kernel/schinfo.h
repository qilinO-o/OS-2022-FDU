#pragma once

#include <common/list.h>
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
    ListNode rq_node;
};
