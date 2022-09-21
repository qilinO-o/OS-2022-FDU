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
static ListNode* free_mem[CORE_NUM][MAX_ORDER];

extern char end[];

define_early_init(free_mem){
    for(int i=0;i<CORE_NUM;++i){
        for(int j=0;j<MAX_ORDER;++j){
            free_mem[i][j] = NULL;
        }
    }
}

SpinLock* mem_lock;
define_early_init(mem_lock){
    init_spinlock(mem_lock);
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

// TODO: kalloc kfree
void* kalloc(isize size){
    size += 8;
    int idx = __log2(size);
    int pos = idx;
    void* ret = NULL;
    while(idx < MAX_ORDER && free_mem[cpuid()][idx] == NULL){
        ++idx;
    }
    if(idx == MAX_ORDER){
        void* new_page = kalloc_page();
        if(pos != MAX_ORDER){
            for(int i = MAX_ORDER-1; i >= pos; --i){
                void* dst = new_page + (1<<i);
                if(free_mem[cpuid()][i] == NULL){
                    init_list_node((ListNode*)dst);
                    free_mem[cpuid()][i] = (ListNode*)dst;
                }
                else{
                    init_list_node((ListNode*)dst);
                    ((ListNode*)(dst))->next = free_mem[cpuid()][i];
                    free_mem[cpuid()][i]->prev = (ListNode*)(dst);
                    free_mem[cpuid()][i] = (ListNode*)(dst);
                }
            }
        }
        *(i64*) new_page = pos;
        ret = new_page + 8;
    }
    else{
        void* new_addr = free_mem[cpuid()][idx];
        free_mem[cpuid()][idx] = free_mem[cpuid()][idx]->next;
        if(free_mem[cpuid()][idx] != NULL){
            free_mem[cpuid()][idx]->prev = free_mem[cpuid()][idx];
        }

        for(int i = idx-1; i >= pos; --i){
            void* dst = new_addr + (1<<i);
                if(free_mem[cpuid()][i] == NULL){
                    init_list_node((ListNode*)dst);
                    free_mem[cpuid()][i] = (ListNode*)dst;
                }
                else{
                    init_list_node((ListNode*)dst);
                    ((ListNode*)(dst))->next = free_mem[cpuid()][i];
                    free_mem[cpuid()][i]->prev = (ListNode*)(dst);
                    free_mem[cpuid()][i] = (ListNode*)(dst);
                }
        }
        *(i64*) new_addr = pos;
        ret = new_addr + 8;
    }
    //printk("cpu: %d size: %d  addr: %lld \n",cpuid(),(int)size,((i64)ret & 0x0000FFFFFFFFFFFF));
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
        if(free_mem[cpuid()][idx] == NULL){
            init_list_node((ListNode*)p);
            free_mem[cpuid()][idx] = (ListNode*)p;
        }
        else{
            init_list_node((ListNode*)p);
            ((ListNode*)p)->next = free_mem[cpuid()][idx];
            free_mem[cpuid()][idx]->prev = (ListNode*)p;
            free_mem[cpuid()][idx] = (ListNode*)p;
        }
    }
}
