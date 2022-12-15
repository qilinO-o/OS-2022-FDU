#pragma once

#include <kernel/proc.h>
#include <kernel/schinfo.h>
#include <kernel/pidmap.h>

struct container
{
    struct container* parent;
    struct proc* rootproc;

    struct schinfo schinfo;
    struct schqueue schqueue;

    // TODO: namespace (local pid?)
    pidmap_t pidmap;
};

struct container* create_container(void (*root_entry)(), u64 arg);
void set_container_to_this(struct proc*);
