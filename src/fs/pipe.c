#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>

int pipeAlloc(File** f0, File** f1) {
    // TODO
    // alloc file 0 & 1
    File* new_f0 = filealloc();
    File* new_f1 = filealloc();
    if(new_f0 == NULL || new_f1 == NULL){
        return -1;
    }
    // alloc pipe
    Pipe* new_pipe = kalloc(sizeof(Pipe));
    if(new_pipe == NULL){
        fileclose(new_f0);
        fileclose(new_f1);
        return -1;
    }
    //init file 0 & 1
    memset(new_pipe,0,sizeof(Pipe));
    init_spinlock(&(new_pipe->lock));
    init_sem(&(new_pipe->wlock),0);
    init_sem(&(new_pipe->rlock),0);
    new_f0->pipe = new_pipe;
    new_f0->type = FD_PIPE;
    new_f0->readable = 1;
    new_f1->pipe = new_pipe;
    new_f1->type = FD_PIPE;
    new_f1->writable = 1;
    *f0 = new_f0;
    *f1 = new_f1;
    new_pipe->readopen = 1;
    new_pipe->writeopen = 1;
    return 0;
}

void pipeClose(Pipe* pi, int writable) {
    // TODO
    _acquire_spinlock(&(pi->lock));
    if(writable == 1){
        pi->writeopen = 0;
    }
    else{
        pi->readopen = 0;
    }
    if(pi->writeopen == 0 && pi->readopen == 0){
        kfree((void*)pi);
        return;
    }
    _release_spinlock(&(pi->lock));
}

int pipeWrite(Pipe* pi, u64 addr, int n) {
    // TODO
    char* src = (char*)addr;
    int nwrite = 0;
    _acquire_spinlock(&(pi->lock));
    for(int i=0;i<n;++i){
        // write one byte to pi.data
        if(PIPESIZE-(pi->nwrite-pi->nread)>0){ // have space
            pi->data[pi->nwrite%PIPESIZE] = src[i];
            pi->nwrite++;
            nwrite++;
        }
        else{
            if(pi->readopen == 1){
                _lock_sem(&(pi->wlock));
                _release_spinlock(&(pi->lock));
                post_all_sem(&(pi->rlock));
                // wait for new read(space)
                if(_wait_sem(&(pi->wlock),true) == false){
                    break;
                }
                _acquire_spinlock(&(pi->lock));
            }
            else{
                break;
            }
        }
    }
    _release_spinlock(&(pi->lock));
    post_all_sem(&(pi->rlock));
    return nwrite;
}

int pipeRead(Pipe* pi, u64 addr, int n) {
    // TODO
    char* dst = (char*)addr;
    int nread = 0;
    _acquire_spinlock(&(pi->lock));
    for(int i=0;i<n;++i){
        // read one byte from pi.data
        if(pi->nwrite-pi->nread>0){ // have unread data
            dst[i] = pi->data[pi->nread%PIPESIZE];
            pi->nread++;
            nread++;
        }
        else{
            if(pi->writeopen == 1){
                _lock_sem(&(pi->rlock));
                _release_spinlock(&(pi->lock));
                post_all_sem(&(pi->wlock));
                // wait for new write data
                if(_wait_sem(&(pi->rlock),true) == false){
                    break;
                }
                _acquire_spinlock(&(pi->lock));
            }
            else{
                break;
            }
        }
    }
    _release_spinlock(&(pi->lock));
    post_all_sem(&(pi->wlock));
    return nread;
}