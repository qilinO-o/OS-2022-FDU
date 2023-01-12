#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

//static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file* f);

int execve(const char *path, char *const argv[], char *const envp[]) {
	// TODO
	OpContext ctx;
	bcache.begin_op(&ctx);
	Inode* inode_p = namei(path, &ctx);
	if(inode_p == NULL){
		bcache.end_op(&ctx);
		return -1;
	}
	inodes.lock(inode_p);
	Elf64_Ehdr elf_header;
	usize Ehdr_size = sizeof(Elf64_Ehdr);
	// read elf header from file(inode)
	if(inodes.read(inode_p, (u8*)(&elf_header), 0, Ehdr_size) < Ehdr_size){
		inodes.unlock(inode_p);
		inodes.put(&ctx, inode_p);
		bcache.end_op(&ctx);
		return -1;
	}
	ASSERT(strncmp((const char*)elf_header.e_ident, ELFMAG, 4)==0);
	struct pgdir* exec_pgdir = kalloc(sizeof(struct pgdir));
	init_pgdir(exec_pgdir);
	Elf64_Phdr program_header;
	usize Phdr_size = sizeof(Elf64_Phdr);
	u64 ph_off = elf_header.e_phoff;
	// read all program header and make corresponding section to new pgdir
	u64 end = 0;
	for(int i=0;i<elf_header.e_phnum; ++i){
		if(inodes.read(inode_p, (u8*)(&program_header), ph_off, Phdr_size) < Phdr_size){
			inodes.unlock(inode_p);
			inodes.put(&ctx, inode_p);
			bcache.end_op(&ctx);
			free_pgdir(exec_pgdir);
			return -1;
		}
		ph_off += Phdr_size;
		if(program_header.p_type != PT_LOAD){
			continue;
		}
		u64 section_flag = 0;
		// text section
		if(program_header.p_flags & (PF_R | PF_X)){
			section_flag = ST_TEXT;
			end = program_header.p_vaddr + program_header.p_filesz;
		}
		// data and bss section
		else if(program_header.p_flags & (PF_R | PF_W)){
			section_flag = ST_FILE;
			end = program_header.p_vaddr + program_header.p_memsz;
		}
		else{
			inodes.unlock(inode_p);
			inodes.put(&ctx, inode_p);
			bcache.end_op(&ctx);
			free_pgdir(exec_pgdir);
			return -1;
		}

		struct section *st = kalloc(sizeof(struct section));
		st->begin = program_header.p_vaddr;
		st->end = end;
		st->flags = section_flag;
		init_sleeplock(&(st->sleeplock));
		_insert_into_list(&(exec_pgdir->section_head), &(st->stnode));

		// (1) allocate memory, va region [vaddr, vaddr+filesz)
		// (2) copy [offset, offset + filesz) of file to va [vaddr, vaddr+filesz) of memory
		// copy page by page
		u64 file_sz = program_header.p_filesz;
		u64 off = program_header.p_offset;
		u64 va = program_header.p_vaddr;
		while(file_sz > 0){
			u64 va0 = PAGE_BASE(va);
			u64 sz = MIN(PAGE_SIZE - (va-va0), file_sz);
			void* dest = kalloc(sz);
			u64 pte_flag = PTE_USER_DATA;
			if(section_flag == ST_TEXT){
				pte_flag |= PTE_RO;
			}
			vmmap(exec_pgdir, va0, dest, pte_flag);
			if(inodes.read(inode_p, (u8*)(dest+(va-va0)), off, sz) != sz){
				inodes.unlock(inode_p);
				inodes.put(&ctx, inode_p);
				bcache.end_op(&ctx);
				free_pgdir(exec_pgdir);
				return -1;
			}
			file_sz -= sz;
			off += sz;
			va += sz;
		}
		// [p_vaddr+p_filesz, p_vaddr+p_memsz) the bss section which is required to set to 0
		// COW by using the zero page
		if(section_flag == ST_FILE){
			void* dest = get_zero_page();
			file_sz = program_header.p_memsz - program_header.p_filesz;
			while(file_sz > 0){
				u64 va0 = PAGE_BASE(va);
				u64 sz = MIN(PAGE_SIZE - (va-va0), file_sz);
				vmmap(exec_pgdir, va0, dest, PTE_USER_DATA | PTE_RO);
				file_sz -= sz;
				va += sz;
			}
		}
	}
	inodes.unlock(inode_p);
	inodes.put(&ctx, inode_p);
	bcache.end_op(&ctx);

	// create user stack
	u64 stack_page_size = 5;
	u64 sp = PAGE_BASE(end) + PAGE_SIZE + stack_page_size * PAGE_SIZE;
	for(u64 i=1;i<=stack_page_size;++i){
		void* p = kalloc_page();
		vmmap(exec_pgdir, sp-i*PAGE_SIZE, p, PTE_USER_DATA);
	}
	// fill in stack
	struct proc* this_proc = thisproc();
	
	sp -= 8;
    u64 tmp = 0;
    copyout(exec_pgdir, (void*)sp, &tmp, 8);

	u64 argc = 0;
	if(envp){
		while(envp[argc]){
			++argc;
		}
		for(int i=argc-1;i>=0;--i){
			sp -= (strlen(envp[i]) + 1);
			copyout(exec_pgdir, (void*)sp, envp[i], strlen(envp[i]) + 1);
		}
	}

	sp -= 8;
    copyout(exec_pgdir, (void*)sp, &tmp, 8);
	
	argc = 0;
	if(argv){
		while(argv[argc]){
			++argc;
		}
		for(int i=argc-1;i>=0;--i){
			sp -= (strlen(argv[i]) + 1);
			copyout(exec_pgdir, (void*)sp, argv[i], strlen(argv[i]) + 1);
			if(i == 0){
				this_proc->ucontext->reserved[1] = sp;
			}
		}
	}
	this_proc->ucontext->reserved[0] = argc;
	
	sp -= 8;
    copyout(exec_pgdir, (void*)sp, &argc, 8);

	// change to new exec_pgdir
	struct pgdir* old_pgdir = &(this_proc->pgdir);
	this_proc->ucontext->sp = sp;
	this_proc->ucontext->elr = elf_header.e_entry;
	this_proc->pgdir = *exec_pgdir;
	attach_pgdir(exec_pgdir);
	free_pgdir(old_pgdir);
	return 0;
}
