#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <kernel/schinfo.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

typedef struct UserContext
{
    // TODO: customize your trap frame
    i64 reserved[32];
    i64 spsr;
    i64 elr;
} UserContext;

typedef struct KernelContext
{
    // TODO: customize your context
    i64 reserved[32];
    // i64 lr;
    // i64 x0;
    // i64 x1;
} KernelContext;

struct proc
{
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    ListNode ptnode;
    struct proc* parent;
    struct schinfo schinfo;
    void* kstack;
    UserContext* ucontext;
    KernelContext* kcontext;
};

// void init_proc(struct proc*);
struct proc* create_proc();
int start_proc(struct proc*, void(*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
int wait(int* exitcode);
