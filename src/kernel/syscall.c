#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <kernel/pt.h>
#include <kernel/paging.h>

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

// check if the virtual address [start,start+size) is READABLE by the current user process
bool user_readable(const void* start, usize size) {
    // TODO
    if((u64)start >= KSPACE_MASK){
        return true;
    }
    bool ret = false;
    struct section* st = NULL;
    ListNode* st_head = &(thisproc()->pgdir.section_head);
	_for_in_list(node, st_head){
		if(node == st_head) break;
		st = container_of(node, struct section, stnode);
		if(st->begin <= (u64)start && ((u64)start + size) < st->end){
			ret = true;
            break;
		}
	}
    return ret;
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by the current user process
bool user_writeable(const void* start, usize size) {
    // TODO
    if((u64)start >= KSPACE_MASK){
        return true;
    }
    bool ret = false;
    struct section* st = NULL;
    ListNode* st_head = &(thisproc()->pgdir.section_head);
	_for_in_list(node, st_head){
		if(node == st_head) break;
		st = container_of(node, struct section, stnode);
        if(st->flags & ST_RO){
            continue;
        }
		if(st->begin <= (u64)start && ((u64)start + size) < st->end){
			ret = true;
            break;
		}
	}
    return ret;
}

// get the length of a string including tailing '\0' in the memory space of current user process
// return 0 if the length exceeds maxlen or the string is not readable by the current user process
usize user_strlen(const char* str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}
