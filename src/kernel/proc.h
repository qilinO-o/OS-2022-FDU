#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <kernel/schinfo.h>
#include <kernel/pt.h>
#include <kernel/container.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

typedef struct UserContext
{
    // TODO: customize your trap frame
    u64 spsr;
    u64 elr;
    u64 sp;
    u64 ttbr0;
    u64 reserved[32];
} UserContext;

typedef struct KernelContext
{
    // TODO: customize your context
    u64 lr;
    u64 x0;
    u64 x1;
    u64 reserved[11]; //x19-29
} KernelContext;

struct proc
{
    bool killed;
    bool idle;
    int pid;
    int localpid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    ListNode ptnode;
    struct proc* parent;
    struct schinfo schinfo;
    struct pgdir pgdir;
    struct container* container;
    void* kstack;
    UserContext* ucontext;
    KernelContext* kcontext;
};

// void init_proc(struct proc*);
WARN_RESULT struct proc* create_proc();
void set_parent_to_this(struct proc*);
int start_proc(struct proc*, void(*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int* exitcode, int* pid);
WARN_RESULT int kill(int pid);
