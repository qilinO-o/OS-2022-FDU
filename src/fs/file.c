/* File descriptors */

#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>
#include "fs.h"
#include <common/string.h>
#include <fs/pipe.h>

static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable
    init_spinlock(&(ftable.lock));
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process
    memset(oftable, 0, sizeof(struct oftable));
}

/* Allocate a file structure. */
struct file* filealloc() {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&(ftable.lock));
    for(int i=0;i<NFILE;++i){
        if(ftable.files[i].ref == 0){
            ftable.files[i].ref = 1;
            _release_spinlock(&(ftable.lock));
            return &(ftable.files[i]);
        }
    }
    _release_spinlock(&(ftable.lock));
    return 0;
}

/* Increment ref count for file f. */
struct file* filedup(struct file* f) {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&(ftable.lock));
    f->ref++;
    _release_spinlock(&(ftable.lock));
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void fileclose(struct file* f) {
    /* TODO: Lab10 Shell */
    ASSERT(f->type != FD_NONE);
    _acquire_spinlock(&(ftable.lock));
    f->ref--;
    if(f->ref <= 0){
        File* temp = f;
        f->ref = 0;
        f->type = FD_NONE;
        _release_spinlock(&(ftable.lock));
        if(temp->type == FD_PIPE){
            pipeClose(f->pipe, f->writable);
        }
        else{
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.put(&ctx, temp->ip);
            bcache.end_op(&ctx);
        }
    }
    _release_spinlock(&(ftable.lock));
}

/* Get metadata about file f. */
int filestat(struct file* f, struct stat* st) {
    /* TODO: Lab10 Shell */
    if(f->type == FD_INODE){
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    return -1;
}

/* Read from file f. */
isize fileread(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if(f->readable != 1){
        return -1;
    }
    if(f->type == FD_INODE){
        inodes.lock(f->ip);
        usize nread = inodes.read(f->ip, (u8*)addr, f->off, (usize)n);
        f->off += nread;
        inodes.unlock(f->ip);
        return nread;
    }
    else if(f->type == FD_PIPE){
        return pipeRead(f->pipe, (u64)addr, n);
    }
    else{
        PANIC();
    }
    return 0;
}

/* Write to file f. */
isize filewrite(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if(f->writable != 1){
        return -1;
    }
    if(f->type == FD_INODE){
        usize max_valid_write_n = INODE_MAX_BYTES - f->off;
        n = MIN((usize)n, max_valid_write_n);
        usize nwrite = 0;
        while(nwrite < (usize)n){
            usize op_n = MIN((usize)(n-nwrite), (usize)MAX_OP_WRITE_N);
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.lock(f->ip);
            usize w = inodes.write(&ctx, f->ip, (u8*)(addr + nwrite), f->off, op_n);
            f->off += w;
            inodes.unlock(f->ip);
            bcache.end_op(&ctx);
            nwrite += w;
        }
        return nwrite;
    }
    else if(f->type == FD_PIPE){
        return pipeWrite(f->pipe, (u64)addr, n);
    }
    else{
        PANIC();
    }
    return 0;
}
