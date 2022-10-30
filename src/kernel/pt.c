#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

#include <driver/memlayout.h>
#include <kernel/printk.h>

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc){
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    PTEntriesPtr pt0 = pgdir->pt;
    PTEntriesPtr pt1 = NULL;
    PTEntriesPtr pt2 = NULL;
    PTEntriesPtr pt3 = NULL;
    //PTEntry pa = NULL;
    if(pt0 == NULL){
        if(alloc){
            pgdir->pt = kalloc_page();
            memset(pgdir->pt,0,PAGE_SIZE);
        }
        else{
            return NULL;
        }
    }
    pt0 = pgdir->pt;
    pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[VA_PART0(va)]));
    if(!(pt0[VA_PART0(va)] & PTE_VALID)){
        if(alloc){
            pt1 = kalloc_page();
            memset(pt1,0,PAGE_SIZE);
            pt0[VA_PART0(va)] = K2P(pt1) | PTE_TABLE;
        }
        else{
            return NULL;
        }
    }
    pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[VA_PART1(va)]));
    if(!(pt1[VA_PART1(va)] & PTE_VALID)){
        if(alloc){
            pt2 = kalloc_page();
            memset(pt2,0,PAGE_SIZE);
            pt1[VA_PART1(va)] = K2P(pt2) | PTE_TABLE;
        }
        else{
            return NULL;
        }
    }
    pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[VA_PART2(va)]));
    if(!(pt2[VA_PART2(va)] & PTE_VALID)){
        if(alloc){
            pt3 = kalloc_page();
            memset(pt3,0,PAGE_SIZE);
            pt2[VA_PART2(va)] = K2P(pt3) | PTE_TABLE;
        }
        else{

            return NULL;
        }
    }
    //pa = (PTEntry)P2K(PTE_ADDRESS(pt3[VA_PART3(va)]));
    if(!(pt3[VA_PART3(va)] & PTE_VALID)){
        if(alloc){
            // pa = (PTEntry)(K2P(va) | PTE_TABLE);
            // pt3[VA_PART3(va)] = pa;
        }
        else{
            return NULL;
        }
    }
    return &(pt3[VA_PART3(va)]);
}

void init_pgdir(struct pgdir* pgdir){
    pgdir->pt = NULL;
}

void free_pgdir(struct pgdir* pgdir){
    // TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    PTEntriesPtr pt0 = pgdir->pt;
    if(pt0 == NULL){
        return;
    }
    for(int i=0;i<N_PTE_PER_TABLE;++i){
        if(pt0[i] & PTE_VALID){
            PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[i]));
            for(int j=0;j<N_PTE_PER_TABLE;++j){
                if(pt1[j] & PTE_VALID){
                    PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[j]));
                    for(int k=0;k<N_PTE_PER_TABLE;++k){
                        if(pt2[k] & PTE_VALID){
                            PTEntriesPtr pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[k]));
                            kfree_page((void*)pt3);
                        }
                    }
                    kfree_page((void*)pt2);
                }
            }
            kfree_page((void*)pt1);
        }
    }
    kfree_page((void*)pt0);
    pgdir->pt = NULL;
}

void attach_pgdir(struct pgdir* pgdir){
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}



