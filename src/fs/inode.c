#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

// this lock mainly prevents concurrent access to inode list `head`, reference
// count increment and decrement.
static SpinLock lock;
static ListNode head;

static const SuperBlock* sblock;
static const BlockCache* cache;

// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);
    // TODO
    for(u32 i=1;i<sblock->num_inodes;++i){
        Block* block = cache->acquire(to_block_no(i));
        InodeEntry* inode_entry = get_entry(block,i);
        if(inode_entry->type == INODE_INVALID){
            memset(inode_entry,0,sizeof(*inode_entry));
            inode_entry->type = type;
            cache->sync(ctx,block);
            cache->release(block);
            return i;
        }
        cache->release(block);
    }
    PANIC();
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    unalertable_wait_sem(&(inode->lock));
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    post_sem(&(inode->lock));
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    Block* block = cache->acquire(to_block_no(inode->inode_no));
    InodeEntry* inode_entry = get_entry(block,inode->inode_no);
    if(inode->valid && do_write){
        memmove(inode_entry, &(inode->entry), sizeof(inode->entry));
        cache->sync(ctx, block);
    }
    else if(inode->valid == false){
        inode->valid = true;
        memmove(&(inode->entry), inode_entry, sizeof(inode->entry));
    }
    cache->release(block);
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    _acquire_spinlock(&lock);
    // TODO
    ListNode* p = head.next;
    Inode* inode = NULL;
    while(p!=&head){
        inode = container_of(p,Inode,node);
        if(inode->inode_no == inode_no && inode->rc.count > 0){
            _increment_rc(&(inode->rc));
            _release_spinlock(&lock);
            // make sure the inode got from list is valid
            inode_lock(inode);
            inode_unlock(inode);
            return inode;
        }
        p = p->next;
    }
    inode = (Inode*)kalloc(sizeof(Inode));
    init_inode(inode);
    _merge_list(&head,&(inode->node));
    inode->inode_no = inode_no;
    _increment_rc(&(inode->rc));
    inode_lock(inode);
    _release_spinlock(&lock);
    inode_sync(NULL, inode, false);
    inode_unlock(inode);

    return inode;
    //return NULL;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    InodeEntry* inode_entry = &(inode->entry);
    for(int i=0;i<INODE_NUM_DIRECT;++i){
        if(inode_entry->addrs[i]!=0){
            cache->free(ctx,inode_entry->addrs[i]);
        }
    }
    memset(inode_entry->addrs,0,sizeof(inode_entry->addrs));
    if(inode_entry->indirect != 0){
        Block* block = cache->acquire(inode_entry->indirect);
        u32* addrs = get_addrs(block);
        for(usize i=0;i<INODE_NUM_INDIRECT;++i){
            if(addrs[i]!=0){
                cache->free(ctx,addrs[i]);
            }
        }
        cache->release(block);
        cache->free(ctx,inode_entry->indirect);
        inode_entry->indirect = 0;
    }
    inode_entry->num_bytes = 0;
    inode_sync(ctx, inode, true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    _increment_rc(&inode->rc);
    _release_spinlock(&lock);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    if(inode->rc.count == 1 && inode->entry.num_links == 0 && inode->valid){
        inode_lock(inode);
        _detach_from_list(&(inode->node));
        _release_spinlock(&lock);
        inode_clear(ctx,inode);
        inode->entry.type = INODE_INVALID;
        inode_sync(ctx,inode,true);
        inode->valid = false;
        inode_unlock(inode);
        kfree((void*)inode);
        return;
    }
    _decrement_rc(&(inode->rc));
    _release_spinlock(&lock);
}

// this function is private to inode layer, because it can allocate block
// at arbitrary offset, which breaks the usual file abstraction.
//
// retrieve the block in `inode` where offset lives. If the block is not
// allocated, `inode_map` will allocate a new block and update `inode`, at
// which time, `*modified` will be set to true.
// the block number is returned.
//
// NOTE: caller must hold the lock of `inode`.
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    InodeEntry* inode_entry = &(inode->entry);
    *modified = false;
    if(offset < INODE_NUM_DIRECT){
        usize bno = inode_entry->addrs[offset];
        if(bno == 0){
            bno = cache->alloc(ctx);
            inode_entry->addrs[offset] = bno;
            *modified = true;
        }
        return bno;
    }
    else if(offset - INODE_NUM_DIRECT < INODE_NUM_INDIRECT){
        offset -= INODE_NUM_DIRECT;
        usize bno = inode_entry->indirect;
        if(bno == 0){
            bno = cache->alloc(ctx);
            inode_entry->indirect = bno;
            *modified = true;
        }
        Block* block = cache->acquire(bno);
        u32* addrs = get_addrs(block);
        bno = addrs[offset];
        if(bno == 0){
            bno = cache->alloc(ctx);
            addrs[offset] = bno;
            *modified = true;
            cache->sync(ctx, block);
        }
        cache->release(block);
        return bno;
    }
    else{
        PANIC();
    }
    return 0;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    bool modified = false;
    for(usize have_read = 0,sz = 0;have_read < count;have_read+=sz){
        Block* block = cache->acquire(inode_map(NULL,inode,offset / BLOCK_SIZE,&modified));
        if(count - have_read < BLOCK_SIZE - offset % BLOCK_SIZE){
            sz = count - have_read;
        }
        else{
            sz = BLOCK_SIZE - offset % BLOCK_SIZE;
        }
        memmove(dest, block->data + offset % BLOCK_SIZE, sz);
        cache->release(block);
        dest += sz;
        offset += sz;
    }
    ASSERT(modified == false);
    return count;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    bool modified = false;
    for(usize have_write = 0,sz = 0;have_write < count;have_write+=sz){
        bool this_modified = false;
        Block* block = cache->acquire(inode_map(ctx,inode,offset / BLOCK_SIZE, &this_modified));
        modified = this_modified || modified;
        if(count - have_write < BLOCK_SIZE - offset % BLOCK_SIZE){
            sz = count - have_write;
        }
        else{
            sz = BLOCK_SIZE - offset % BLOCK_SIZE;
        }
        memmove(block->data + offset % BLOCK_SIZE, src, sz);
        cache->sync(ctx,block);
        cache->release(block);
        src += sz;
        offset += sz;
    }
    if(modified || end > inode->entry.num_bytes){
        inode->entry.num_bytes = end;
        inode_sync(ctx, inode, true);
    }
    return count;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    u32 sz = sizeof(DirEntry);
    for(u32 i=0;i<inode->entry.num_bytes;i+=sz){
        DirEntry dir_entry;
        inode_read(inode,(u8*)(&dir_entry),i,sz);
        if(dir_entry.inode_no != 0 && strncmp(dir_entry.name, name,FILE_NAME_MAX_LENGTH) == 0){
            if(index != NULL){
                *index = i;
            }
            return dir_entry.inode_no;
        }
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    if(inode_lookup(inode,name,NULL) != 0){
        return -1;
    }
    u32 sz = sizeof(DirEntry);
    u32 i = 0;
    DirEntry dir_entry;
    for(;i<inode->entry.num_bytes;i+=sz){
        inode_read(inode,(u8*)(&dir_entry),i,sz);
        if(dir_entry.inode_no == 0){
            break;
        }
    }
    strncpy(dir_entry.name, name, FILE_NAME_MAX_LENGTH);
    dir_entry.inode_no = inode_no;
    inode_write(ctx,inode,(u8*)(&dir_entry),i,sz);
    return i;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    DirEntry dir_entry = {0};
    inode_write(ctx,inode,(u8*)(&dir_entry),index,sizeof(DirEntry));
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};
