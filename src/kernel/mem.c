#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>
#include <fs/cache.h>
#include <common/string.h>

RefCount alloc_page_cnt;
RefCount _left_page_cnt;
#define MIN_BLOCK_SIZE 8 
#define MIN_BLOCK_ORDER 3 // log2(MIN_BLOCK_SIZE)
#define PAGES_REF_SIZE (sizeof(struct page))*(PHYSTOP/PAGE_SIZE)
#define PAGES_REF_PAGE_NUM  PAGES_REF_SIZE/PAGE_SIZE
#define MAX_ORDER PAGE_SIZE/MIN_BLOCK_SIZE // 4096 / MIN_BLOCK_SIZE
#define CORE_NUM 4
define_early_init(alloc_page_cnt){
    init_rc(&alloc_page_cnt);
}

static QueueNode* pages;
static QueueNode* free_mem[MAX_ORDER];
static void* zero_page_ptr;
struct page* pages_ref; // index is count by phyical addr

extern char end[];

static SpinLock mem_list_lock;
define_early_init(mem_list_lock){
    init_spinlock(&mem_list_lock);
}

define_early_init(pages){
    init_rc(&_left_page_cnt);
    zero_page_ptr = NULL;
    pages_ref = (struct page*)(P2K(PHYSTOP)-PAGES_REF_SIZE);
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE ; p < P2K(PHYSTOP)-PAGES_REF_SIZE; p += PAGE_SIZE){
        add_to_queue(&pages, (QueueNode*)p); 
        _increment_rc(&_left_page_cnt);
    }
}

define_init(zero_page){
    zero_page_ptr = kalloc_page();
    memset(zero_page_ptr,0,PAGE_SIZE);
    u64 page_num = ((u64)K2P(zero_page_ptr))/PAGE_SIZE;
    pages_ref[page_num].ref.count = 1;
}

void* kalloc_page(){
    _increment_rc(&alloc_page_cnt);
    // TODO
    _decrement_rc(&_left_page_cnt);
    void* ret = fetch_from_queue(&pages);
    u64 page_num = ((u64)K2P(ret))/PAGE_SIZE;
    pages_ref[page_num].ref.count = 0;
    init_spinlock(&(pages_ref[page_num].ref_lock));
    return ret;
}

void kfree_page(void* p){
    _decrement_rc(&alloc_page_cnt);
    // TODO
    u64 page_num = ((u64)K2P(p))/PAGE_SIZE;
    _acquire_spinlock(&(pages_ref[page_num].ref_lock));
    _decrement_rc(&(pages_ref[page_num].ref));
    if(pages_ref[page_num].ref.count <= 0){
        add_to_queue(&pages, (QueueNode*)p);
        _increment_rc(&_left_page_cnt);
    }
    _release_spinlock(&(pages_ref[page_num].ref_lock));
}

int __get_idx(i64 sz){
    int idx = sz >> MIN_BLOCK_ORDER;
    idx = (sz == (idx*MIN_BLOCK_SIZE)) ? idx - 1 : idx;
    return idx;
}

// TODO: kalloc kfree
void* kalloc(isize size){
    //printk("in kalloc\n");
    setup_checker(ch_kalloc);
    size += 8;
    int idx = __get_idx(size);
    size = (idx+1)*MIN_BLOCK_SIZE;
    int pos = idx;
    void* ret = NULL;
    
    acquire_spinlock(ch_kalloc, &mem_list_lock);
    //printk("pp\n");
    while(idx < MAX_ORDER && free_mem[idx] == NULL){
        ++idx;
        
    }
    if(idx == MAX_ORDER){
        //printk("1\n");
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
        //printk("2\n");
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
    release_spinlock(ch_kalloc, &mem_list_lock);
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

u64 left_page_cnt(){
    return _left_page_cnt.count;
}

WARN_RESULT void* get_zero_page(){
    return zero_page_ptr;
}

bool check_zero_page(){
    void* zp = get_zero_page();
    for (usize i = 0; i < PAGE_SIZE; i++) {
        u8 c = ((u8*)zp)[i];
        if (c != 0){
            return false;
        }
    }
    return true;
}

u32 write_page_to_disk(void* ka){
    u32 bno = find_and_set_8_blocks();
    for(u32 i=0;i<8;++i){
        Block* block = bcache.acquire(bno+i);
        memmove(block->data,(ka+i*BLOCK_SIZE),BLOCK_SIZE);
        bcache.sync(NULL,block);
        bcache.release(block);
    }
    return bno;
}

void read_page_from_disk(void* ka, u32 bno){
    for(u32 i=0;i<8;++i){
        Block* block = bcache.acquire(bno+i);
        memmove((ka+i*BLOCK_SIZE),block->data,BLOCK_SIZE);
        bcache.release(block);
    }
    release_8_blocks(bno);
}