#include <kernel/proc.h>
#include <aarch64/mmu.h>
#include <fs/block_device.h>
#include <fs/cache.h> 
#include <kernel/paging.h>
#include <common/defines.h>
#include <kernel/pt.h>
#include <common/sem.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <driver/sd.h>

// define_rest_init(paging){
// 	//TODO init
// 	sd_init();
// 	init_block_device();
// 	init_bcache(get_super_block(),&block_device);
// }

u64 sbrk(i64 size){
	//TODO
	struct proc* p = thisproc();
	struct section* st = NULL;
	_for_in_list(node, &(p->pgdir.section_head)){
		if(node == &(p->pgdir.section_head)) break;
		st = container_of(node, struct section, stnode);
		if(st->flags & ST_HEAP){
			break;
		}
	}
	ASSERT(st!=NULL);
	u64 ret = st->end;
	if(size>=0){
		st->end += size*PAGE_SIZE;
	}
	else{
		ASSERT((u64)(-size)*PAGE_SIZE <= (st->end-st->begin));
		st->end += size*PAGE_SIZE;
		if(st->flags & ST_SWAP){
			swapin(&(p->pgdir), st);
		}
		for(int i=0;i<(-size);++i){
			PTEntriesPtr entry_ptr = get_pte(&(p->pgdir), st->end+i*PAGE_SIZE, false);
			if(entry_ptr != NULL && ((*entry_ptr) & PTE_VALID)){
				void* ka = (void*)P2K(PTE_ADDRESS(*entry_ptr));
				kfree_page(ka);
				*(entry_ptr) = 0;
			}
		}
	}
	arch_tlbi_vmalle1is();
	return ret;
}	


void* alloc_page_for_user(){
	while(left_page_cnt() <= REVERSED_PAGES){ //this is a soft limit
		//TODO
		struct proc* proc_to_swap = get_offline_proc();
		if(proc_to_swap == NULL){
			break;
		}
		_for_in_list(node, &(proc_to_swap->pgdir.section_head)){
			if(node == &(proc_to_swap->pgdir.section_head)) break;
			struct section* st = container_of(node, struct section, stnode);
			if(st->flags & ST_SWAP){
				continue;
			}
			proc_to_swap->pgdir.online = 1;
			swapout(&(proc_to_swap->pgdir), st);
			break;
		}
	}
	return kalloc_page();
}

//caller must have the pd->lock
void swapout(struct pgdir* pd, struct section* st){
	ASSERT(!(st->flags & ST_SWAP));
	st->flags |= ST_SWAP;
	//TODO
	setup_checker(ch_swapout);
	u64 st_begin = st->begin;
	u64 st_end = st->end;
	for(u64 addr = st_begin; addr<st_end; addr+=PAGE_SIZE){
		PTEntriesPtr entry_ptr = get_pte(pd, addr, false);
		if(entry_ptr!=NULL && (*entry_ptr) != 0){
			*entry_ptr &= (~PTE_VALID);
		}
	}
	unalertable_acquire_sleeplock(ch_swapout, &(st->sleeplock));
	_release_spinlock(&(pd->lock));
	if(st->flags & ST_FILE){
		//TODO
	}
	else{
		for(u64 addr = st_begin; addr<st_end; addr+=PAGE_SIZE){
			PTEntriesPtr entry_ptr = get_pte(pd, addr, false);
			if(entry_ptr == NULL || (*entry_ptr) == 0){
				continue;
			}
			void* ka = (void*)P2K(PTE_ADDRESS(*entry_ptr));
			u32 bno = write_page_to_disk(ka);
			kfree_page(ka);
			*entry_ptr = (bno<<12);
		}
	}
	release_sleeplock(ch_swapout, &(st->sleeplock));
}
//Free 8 continuous disk blocks
void swapin(struct pgdir* pd, struct section* st){
	ASSERT(st->flags & ST_SWAP);
	//TODO
	setup_checker(ch_swapin);
	unalertable_acquire_sleeplock(ch_swapin, &(st->sleeplock));
	for(u64 addr = st->begin; addr<st->end; addr+=PAGE_SIZE){
		PTEntriesPtr entry_ptr = get_pte(pd, addr, false);
		if(entry_ptr == NULL || (*entry_ptr) == 0){
			continue;
		}
		u32 bno = (*entry_ptr)>>12;
		void* ka = alloc_page_for_user();
		read_page_from_disk(ka, bno);
		vmmap(pd, addr, ka, PTE_USER_DATA);
	}
	release_sleeplock(ch_swapin, &(st->sleeplock));
	st->flags &= ~ST_SWAP;
}

int pgfault(u64 iss){
	struct proc* p = thisproc();
	struct pgdir* pd = &p->pgdir;
	u64 addr = arch_get_far();
	//TODO
	struct section* st = NULL;
	_for_in_list(node, &(pd->section_head)){
		if(node == &(pd->section_head)) break;
		st = container_of(node, struct section, stnode);
		if(st->begin <= addr && addr < st->end){
			break;
		}
	}
	ASSERT(st!=NULL);
	ASSERT(addr >= st->begin && addr < st->end);
	if(st->flags & ST_SWAP){
		swapin(pd, st);
	}
	PTEntriesPtr ptentry_ptr = get_pte(pd, addr, true);
	if(*ptentry_ptr == 0){ // lazy allocation
		void* new_page = alloc_page_for_user();
		vmmap(pd, addr, new_page, PTE_USER_DATA);
	}
	else if(PTE_FLAGS(*ptentry_ptr) & PTE_RO){ // copy on write
		void* new_page = alloc_page_for_user();
		void* original_page = (void*)P2K(PTE_ADDRESS(*ptentry_ptr));
		memcpy(new_page, original_page, PAGE_SIZE);
		kfree_page(original_page);
		vmmap(pd, addr, new_page, PTE_USER_DATA);
	}
	if(!(PTE_FLAGS(*ptentry_ptr) & PTE_VALID)){
		PANIC();
	}
	arch_tlbi_vmalle1is();
	iss = iss;
	return 0;
}

void init_sections(ListNode* section_head){
	struct section *st = kalloc(sizeof(struct section));
	st->begin = 0x0;
	st->end = 0x0;
	st->flags = 0;
	st->flags |= ST_HEAP;
	init_sleeplock(&(st->sleeplock));
	_insert_into_list(section_head, &(st->stnode));
}

void free_sections(struct pgdir* pd){
	ListNode* node = pd->section_head.next;
	struct section* st = NULL;
	while(node){
		if(node == &(pd->section_head)){
			break;
		}
		st = container_of(node, struct section, stnode);
		if(st->flags & ST_SWAP){
			swapin(pd, st);
		}
		for(u64 i=PAGE_BASE(st->begin); i<st->end; i+=PAGE_SIZE){
			PTEntriesPtr pte_p = get_pte(pd, i, false);
			if(*pte_p && (*pte_p & PTE_VALID)){
				kfree_page((void*)P2K(PTE_ADDRESS(*pte_p)));
			}
		}
		node = node->next;
		kfree((void*)st);
	}
}

void copy_sections(ListNode* from_head, ListNode* to_head){
	_for_in_list(node, from_head){
		if(node == from_head){
			break;
		}
		struct section* st = container_of(node, struct section, stnode);
		struct section* new_st = kalloc(sizeof(struct section));
		memmove(new_st, st, sizeof(struct section));
		_insert_into_list(to_head, &(new_st->stnode));
	}
}