#pragma once

#include <common/spinlock.h>
#include <common/bitmap.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/string.h>

#define PID_MAX_DEFAULT 32768
#define LOCAL_PID_MAX_DEFAULT 256
/***************** pid - proc map ********************/
typedef struct pidmap{
    int pid_base;
    int pid_namespace_size;
    SpinLock pid_base_lock;
    unsigned int nr_free;
    BitmapCell* page;
} pidmap_t;

static INLINE void init_pidmap(pidmap_t* pidmap, int size){
    pidmap->pid_namespace_size = size;
    pidmap->nr_free = size;
    pidmap->pid_base = -1;
    init_spinlock(&pidmap->pid_base_lock);
    pidmap->page = (BitmapCell*)kalloc(size/8);
    memset(pidmap->page,0,sizeof(pidmap->page));
}

static INLINE int test_and_set_pid(int offset, BitmapCell* bitmap){
    int ret = (int)bitmap_get(bitmap, offset);
    if(ret == 0) bitmap_set(bitmap, offset);
    return ret;
}
static INLINE int find_next_zero_bit(BitmapCell* bitmap, int size, int offset){
    while(offset < size){
        if(bitmap_get(bitmap, offset) == 0){
            break;
        }
        ++offset;
    }
    return offset;
}
static INLINE int get_next_pid(pidmap_t* pidmap){
    _acquire_spinlock(&pidmap->pid_base_lock);
    int pid = pidmap->pid_base + 1;
    int offset = pid & (pidmap->pid_namespace_size-1);
    if(pidmap->nr_free == 0){
        _release_spinlock(&pidmap->pid_base_lock);
        return -1;
    }
    offset = find_next_zero_bit(pidmap->page, pidmap->pid_namespace_size, offset);
    if(offset == pidmap->pid_namespace_size) offset = find_next_zero_bit(pidmap->page, offset-1, 0);
    if((offset != pidmap->pid_namespace_size) && (!test_and_set_pid(offset, pidmap->page))){
        --pidmap->nr_free;
        pidmap->pid_base = offset;
        _release_spinlock(&pidmap->pid_base_lock);
        return offset;
    }
    _release_spinlock(&pidmap->pid_base_lock);
    return -1;
}
static INLINE void free_pid(pidmap_t* pidmap, int pid){
    _acquire_spinlock(&pidmap->pid_base_lock);
    int offset = pid & (pidmap->pid_namespace_size-1);
    ASSERT(pid < pidmap->pid_namespace_size);
    ++pidmap->nr_free;
    bitmap_clear(pidmap->page, offset);
    _release_spinlock(&pidmap->pid_base_lock);
}

/***************** pid - proc map ********************/