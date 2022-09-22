#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>

RefCount alloc_page_cnt;
#define MAX_ORDER 12 //4096 B
#define CORE_NUM 4
define_early_init(alloc_page_cnt){
    init_rc(&alloc_page_cnt);
}

static QueueNode* pages;
static ListNode free_mem[CORE_NUM][MAX_ORDER];

extern char end[];

define_early_init(free_mem){ //init list head
    for(int i=0;i<CORE_NUM;++i){
        for(int j=0;j<MAX_ORDER;++j){
            free_mem[i][j].prev = &free_mem[i][j];
            free_mem[i][j].next = NULL;
        }
    }
}

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

int __log2(i64 num){
    int ret = 0, n = num;
    while(num != 1){
        num >>= 1;
        ++ret;
    }
    if((1<<ret) < n) ++ret;
    return ret;
}

void merge_node(void* node, int idx){
    int up = MAX_ORDER;
    for(int i = idx; i < up; ++i){
        void* dst;
        int flag = 0;
        if((i64)node % (1<<(i+1)) == 0){
            dst = node + (1<<i);
        }
        else{
            dst = node - (1<<i);
        }
        _for_in_list(l_node, &free_mem[cpuid()][i]){
            if(l_node == dst){
                flag = 1;
                break;
            }
        }
        if(flag){
            node = (node < dst) ? node : dst; 
            _detach_from_list(dst);
            if(i == up-1){
                // _insert_into_list(&free_mem[cpuid()][i],node);
                kfree_page(node);
            }
        }
        else{
            _insert_into_list(&free_mem[cpuid()][i],node);
            break;
        }
    }
}

// TODO: kalloc kfree
void* kalloc(isize size){
    size += 8;
    int idx = __log2(size);
    int pos = idx;
    void* ret = NULL;
    while(idx < MAX_ORDER && free_mem[cpuid()][idx].next == NULL){
        ++idx;
    }
    if(idx == MAX_ORDER){
        void* new_page = kalloc_page();
        if(pos != MAX_ORDER){
            for(int i = pos; i <= MAX_ORDER-1; ++i){
                void* dst = new_page + (1<<i);
                merge_node(dst,i);
            }
        }
        *(i64*) new_page = pos;
        ret = new_page + 8;
    }
    else{
        void* new_addr = free_mem[cpuid()][idx].next;
        _detach_from_list(new_addr);
        for(int i = pos; i <= idx-1; ++i){
            void* dst = new_addr + (1<<i);
            merge_node(dst,i);
        }
        *(i64*) new_addr = pos;
        ret = new_addr + 8;
    }
    //printk("cpu: %d size: %d  addr: %p \n",cpuid(),(int)size,ret);
    return ret;
}

void kfree(void* p){
    //printk("1");
    p -= 8;
    int idx = *(int*)(p);
    if(idx == MAX_ORDER){
        kfree_page(p);
    }
    else{
        merge_node(p,idx);
    }
}