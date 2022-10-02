#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>

RefCount alloc_page_cnt;
#define MIN_BLOCK_SIZE 8 
#define MIN_BLOCK_ORDER 3 // log2(MIN_BLOCK_SIZE)
#define MAX_ORDER PAGE_SIZE/MIN_BLOCK_SIZE // 4096 / MIN_BLOCK_SIZE
#define CORE_NUM 4
define_early_init(alloc_page_cnt){
    init_rc(&alloc_page_cnt);
}

static QueueNode* pages;
static QueueNode* free_mem[MAX_ORDER];

extern char end[];

SpinLock* mem_list_lock;
define_early_init(mem_list_lock){
    init_spinlock(mem_list_lock);
}

define_early_init(pages){
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE){
        add_to_queue(&pages, (QueueNode*)p); 
    }
}

void* kalloc_page(){
    _increment_rc(&alloc_page_cnt);
    // TODO
    return fetch_from_queue(&pages);
}

void kfree_page(void* p){
    _decrement_rc(&alloc_page_cnt);
    // TODO
    add_to_queue(&pages, (QueueNode*)p);
}

int __get_idx(i64 sz){
    int idx = sz >> MIN_BLOCK_ORDER;
    idx = (sz == (idx*MIN_BLOCK_SIZE)) ? idx - 1 : idx;
    return idx;
}

// TODO: kalloc kfree
void* kalloc(isize size){
    setup_checker(ch_kalloc);
    size += 8;
    int idx = __get_idx(size);
    size = (idx+1)*MIN_BLOCK_SIZE;
    int pos = idx;
    void* ret = NULL;
    acquire_spinlock(ch_kalloc, mem_list_lock);
    while(idx < MAX_ORDER && free_mem[idx] == NULL){
        ++idx;
    }
    if(idx == MAX_ORDER){
        void* new_page = kalloc_page();
        if(size < PAGE_SIZE){
            void* dst = new_page + size;
            int dst_idx = (PAGE_SIZE - size) / MIN_BLOCK_SIZE - 1;
            add_to_queue(&free_mem[dst_idx], (QueueNode*)dst);
        }
        *(i64*) new_page = pos;
        ret = new_page + 8;
    }
    else{
        void* new_addr = (void*)fetch_from_queue(&free_mem[idx]);
        int rest = (idx+1)*MIN_BLOCK_SIZE - size;
        if(rest != 0){
            void* dst = new_addr + size;
            int dst_idx = rest / MIN_BLOCK_SIZE - 1;
            add_to_queue(&free_mem[dst_idx], (QueueNode*)dst);
        }
        *(i64*) new_addr = pos;
        ret = new_addr + 8;
    }
    release_spinlock(ch_kalloc, mem_list_lock);
    return ret;
}

void kfree(void* p){
    p -= 8;
    int idx = *(int*)(p);
    if(idx == MAX_ORDER-1){
        kfree_page(p);
    }
    else{
        add_to_queue(&free_mem[idx],p);
    }
}