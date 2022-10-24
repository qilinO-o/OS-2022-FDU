#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context){
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = context->reserved[8], ret = 0;
    u64 x0 = context->reserved[0];
    u64 x1 = context->reserved[1];
    u64 x2 = context->reserved[2];
    u64 x3 = context->reserved[3];
    u64 x4 = context->reserved[4];
    u64 x5 = context->reserved[5];
    void* sys_func = syscall_table[id];
    if (id < NR_SYSCALL){
        if(sys_func != NULL){
            ret = ((u64(*)(u64,u64,u64,u64,u64,u64))(sys_func))(x0,x1,x2,x3,x4,x5);
            context->reserved[0] = ret;
        }
        else{
            PANIC();
        }
    }
}
