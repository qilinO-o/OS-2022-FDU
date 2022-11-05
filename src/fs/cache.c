#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

static const SuperBlock* sblock;
static const BlockDevice* device;

static SpinLock lock;     // protects block cache.
static ListNode head;     // the list of all allocated in-memory block.
static LogHeader header;  // in-memory copy of log header block.
static usize cache_cnt;
static Bitmap(bitmap,BLOCK_SIZE); //per bitmap block

// hint: you may need some other variables. Just add them here.
struct LOG {
    /* data */
    int max_size;
    int outstanding;
    int log_used;
    bool committing;
    SpinLock log_lock;
    Semaphore log_sem;
    Semaphore log_commit_sem;
} log;

// read the content from disk.
static INLINE void device_read(Block* block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block* block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8*)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8*)&header);
}

// initialize a block struct.
static void init_block(Block* block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    // TODO
    return cache_cnt;
}

// see `cache.h`.
static Block* cache_acquire(usize block_no) {
    // TODO
    bool if_back = false;
__back:
    _acquire_spinlock(&lock);
    ListNode* p = head.next;
    Block* cached_block = NULL;
    bool hit = false;
    while(p != &head){
        cached_block = container_of(p,Block,node);
        if(cached_block->block_no == block_no){
            hit = true;
            break;
        }
        p = p->next;
    }
    if(hit){
        if(cached_block->acquired){
            _release_spinlock(&lock);
            ASSERT(wait_sem(&(cached_block->lock))); //_acquire_sleeplock(&cached_block->lock);
            if_back = true;
            goto __back; //ensure the cached_block is still there after sleep
        }
        _detach_from_list(p);
        _merge_list(&head,p); //for LRU
    }
    else{
        if(get_num_cached_blocks() >= EVICTION_THRESHOLD){
            LRU_shrink();
        }
        cached_block = (Block*)kalloc(sizeof(Block));
        ++cache_cnt;
        init_block(cached_block);
        cached_block->block_no = block_no;
        device_read(cached_block);
        cached_block->valid = true;
        _merge_list(&head,&(cached_block->node)); //for LRU
    }
    if(!hit || !if_back){ //this lock can certainly get without wait
        get_sem(&(cached_block->lock));
    }
    cached_block->acquired = true;
    _release_spinlock(&lock);
    return cached_block;
}

//caller should acquire lock of cache list
void LRU_shrink(){
    Block* cached_block = NULL;
    ListNode* p = head.prev;
    while(get_num_cached_blocks() >= EVICTION_THRESHOLD && p != &head){
        cached_block = container_of(p,Block,node);
        if(!cached_block->pinned && !cached_block->acquired){
            p = _detach_from_list(p);
            kfree(cached_block);
            --cache_cnt;
        }
        else{
            p = p->prev;
        }
    }
}

// see `cache.h`.
static void cache_release(Block* block) {
    // TODO
    _acquire_spinlock(&lock);
    block->acquired = false;
    post_sem(&(block->lock));
    _release_spinlock(&lock);
}

void recover_from_log(){
    read_header();
    for(int i=0;i<header.num_blocks;++i){
        Block temp;
        temp.block_no = sblock->log_start + i + 1;
        device_read(&temp);
        temp.block_no = header.block_no[i];
        //ASSERT(temp.block_no < sblock->num_blocks);
        device_write(&temp);
    }
    header.num_blocks = 0;
    write_header();
}
// initialize block cache.
void init_bcache(const SuperBlock* _sblock, const BlockDevice* _device) {
    sblock = _sblock;
    device = _device;

    // TODO
    init_spinlock(&lock);
    init_spinlock(&(log.log_lock));
    init_list_node(&head);
    cache_cnt = 0;
    init_sem(&(log.log_sem), 0);
    init_sem(&(log.log_commit_sem), 0);
    log.max_size = MIN(sblock->num_log_blocks - 1, LOG_MAX_SIZE);
    log.log_used = 0;
    log.committing = false;
    log.outstanding = 0;
    recover_from_log();
}

// see `cache.h`.
static void cache_begin_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&(log.log_lock));
    while(true){
        if(log.committing){
            _lock_sem(&(log.log_sem));
            _release_spinlock(&(log.log_lock));
            ASSERT(_wait_sem(&(log.log_sem), true));
            _acquire_spinlock(&(log.log_lock));
        }
        else if(log.log_used + (int)header.num_blocks + OP_MAX_NUM_BLOCKS > log.max_size){
            _lock_sem(&(log.log_sem));
            _release_spinlock(&(log.log_lock));
            ASSERT(_wait_sem(&(log.log_sem), true));
            _acquire_spinlock(&(log.log_lock));
        }
        else{
            log.outstanding++;
            log.log_used += OP_MAX_NUM_BLOCKS;
            ctx->rm = OP_MAX_NUM_BLOCKS;
            break;
        }
    }
    _release_spinlock(&(log.log_lock));
}

// see `cache.h`.
static void cache_sync(OpContext* ctx, Block* block) {
    // TODO
    if(ctx){
        _acquire_spinlock(&(log.log_lock));
        usize i;
        for(i=0;i<header.num_blocks;++i){
            if(header.block_no[i] == block->block_no){
                break;
            }
        }
        if(i == header.num_blocks){
            header.block_no[i] = block->block_no;
            block->pinned = true;
            if(ctx->rm > 0){
                ctx->rm--;
                log.log_used--;
                header.num_blocks++;
            }
            else{
                PANIC();
            }
        }
        _release_spinlock(&(log.log_lock));
    }
    else{
        //ASSERT(block->block_no < sblock->num_blocks);
        device_write(block);
    }
}

void write_block_to_log(){
    for(int i=0;i<header.num_blocks;++i){
        Block* b_op = cache_acquire(header.block_no[i]);
        //ASSERT(sblock->log_start + 1 + i < sblock->num_blocks);
        device->write(sblock->log_start + 1 + i, &(b_op->data));
        b_op->pinned = false;
        cache_release(b_op);
    }
}
void persist_from_log(){
    u8 data[BLOCK_SIZE];
    for(int i=0;i<header.num_blocks;++i){
        device->read(sblock->log_start + 1 + i, &data);
        Block* b_op = cache_acquire(header.block_no[i]);
        memmove(&(b_op->data), &data, BLOCK_SIZE);
        //ASSERT(b_op->block_no < sblock->num_blocks);
        device_write(b_op);
        cache_release(b_op);
    }
}
void checkpoint(){
    if(header.num_blocks <= 0){
        return;
    }
    // Write blocks to log area
    write_block_to_log();
    write_header();
    // actual write, Copy blocks to original locations
    persist_from_log();
    header.num_blocks = 0;
    write_header();
}
// see `cache.h`.
static void cache_end_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&(log.log_lock));
    log.outstanding--;
    log.log_used -= ctx->rm;
    
    if(log.outstanding == 0){
        log.committing = true;
        checkpoint();
        log.committing = false;
        post_all_sem(&(log.log_commit_sem));
        post_all_sem(&(log.log_sem));
        _release_spinlock(&(log.log_lock));
    }
    else{
        post_all_sem(&(log.log_sem));
        _lock_sem(&(log.log_commit_sem));
        _release_spinlock(&(log.log_lock));
        ASSERT(_wait_sem(&(log.log_commit_sem), true));
    }
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext* ctx) {
    // TODO
    Block* bitmap_block = NULL;
    for(usize index = 0; index < sblock->num_blocks; index += BIT_PER_BLOCK){
        usize bitmap_block_no = index / BIT_PER_BLOCK + sblock->bitmap_start;
        bitmap_block = cache_acquire(bitmap_block_no);
        BitmapCell* bitmap = (BitmapCell*)(bitmap_block->data);
        for(int i=0;i<BIT_PER_BLOCK && index+i<sblock->num_blocks;++i){
            bool if_valid = bitmap_get(bitmap, i);
            if(if_valid == 0){
                bitmap_set(bitmap, i);
                cache_sync(ctx, bitmap_block);
                cache_release(bitmap_block);
                usize alloc_block_no = index + i;
                Block* alloc_block = cache_acquire(alloc_block_no);
                memset(alloc_block->data, 0, BLOCK_SIZE);
                cache_sync(ctx, alloc_block);
                cache_release(alloc_block);
                return alloc_block_no;
            }
        }
        cache_release(bitmap_block);
    }
    PANIC();
    return -1;
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext* ctx, usize block_no) {
    // TODO
    usize bitmap_block_no = block_no / BIT_PER_BLOCK + sblock->bitmap_start;
    Block* bitmap_block = cache_acquire(bitmap_block_no);
    BitmapCell* bitmap = (BitmapCell*)(bitmap_block->data);
    usize index = block_no % BIT_PER_BLOCK;
    bool if_valid = bitmap_get(bitmap, index);
    if(if_valid == false){
        PANIC();
    }
    bitmap_clear(bitmap, index);
    cache_sync(ctx, bitmap_block);
    cache_release(bitmap_block);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};
