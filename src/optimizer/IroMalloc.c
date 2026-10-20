#include "compiler/IroMalloc.h"
#include "compiler/CompilerTools.h"
#include <string.h>

#define FLAGMASK 7

typedef struct Block {
    struct Block* prev_;
    struct Block* next_;
    size_t max_size_;
    size_t size_;
} Block;

typedef struct SubBlockBase {
    size_t size_;
    Block* bp_;
} SubBlockBase;

typedef struct SubBlock {
    size_t size_;
    Block* bp_;
    struct SubBlock* prev_;
    struct SubBlock* next_;
} SubBlock;

typedef struct SubBlockTail {
    size_t size_;
} SubBlockTail;

typedef struct BlockTail {
    size_t size_;
    SubBlock* start_;
} BlockTail;

typedef struct FixStart {
    struct FixBlock* next_;
    struct FixSubBlock* start_;
    size_t n_allocated_;
} FixStart;

typedef struct FixBlock {
    struct FixBlock* reserve_;
    struct FixBlock* next_;
    size_t client_size_;
} FixBlock;

typedef struct FixSubBlockBase {
    FixBlock* block_;
} FixSubBlockBase;

typedef struct FixSubBlock {
    FixBlock* block_;
    struct FixSubBlock* next_;
} FixSubBlock;

#define fix_pool_sizes IRO_fix_pool_sizes

static const size_t fix_pool_sizes[] = {0xC, 0x14, 0x24, 0x44};
static Block* start_;
static FixStart fix_start[4];
static int initialized;

static void Block_link(Block* block, SubBlock* subblock);
static void Block_unlink(Block* block, SubBlock* subblock);
static void SubBlock_construct(
    SubBlock* subblock, size_t size, Block* block, int prevAllocated, int isAllocated
);
static SubBlock* SubBlock_split(SubBlock* subblock, size_t pos);
static SubBlock* SubBlock_merge_prev(SubBlock* subblock, SubBlock** sbptr);
static void SubBlock_merge_next(SubBlock* subblock, SubBlock** sbptr);

#define BLOCK_START(block) \
    (*(SubBlock**)((char*)(block) + ((block)->size_ & ~FLAGMASK) - sizeof(SubBlock*)))

#define BUF_LOCATOR(buf) (*((size_t*)((char*)(buf) - sizeof(size_t))))
#define SBB_BLOCK(sbb) ((Block*)((long)((sbb)->bp_) & ~1))

#define BUF_IS_VAR(buf) (BUF_LOCATOR(buf) & 1)
#define VARBUF_SBB(buf) ((SubBlockBase*)((char*)(buf) - sizeof(SubBlockBase)))
#define VARBUF_SB(buf) ((SubBlock*)((char*)(buf) - sizeof(SubBlockBase)))
#define VARBUF_BLOCKSIZE(buf) (VARBUF_SBB(buf)->size_ & ~FLAGMASK)
#define VARBUF_SIZE(buf) (((VARBUF_SBB(buf)->size_ & ~FLAGMASK) - sizeof(SubBlockBase)))
#define VARBUF_BLOCK(buf) SBB_BLOCK(VARBUF_SBB(buf))
#define FIXBUF_SIZE(buf) \
    (((FixSubBlock*)((char*)(buf) - sizeof(FixSubBlockBase)))->block_->client_size_)

#define BUF_SIZE(buf) BUF_IS_VAR(buf) ? VARBUF_SIZE(buf) : FIXBUF_SIZE(buf)

static void Block_construct(Block* block, size_t size) {
    SubBlock* subblock;

    block->size_ = size | 3;
    ((BlockTail*)((char*)block + size - sizeof(BlockTail)))->size_ = block->size_;

    subblock = (SubBlock*)(block + 1);
    SubBlock_construct(subblock, size - sizeof(Block) - sizeof(BlockTail), block, 0, 0);

    block->max_size_ = size - sizeof(Block) - sizeof(BlockTail);
    BLOCK_START(block) = NULL;

    Block_link(block, subblock);
}

static SubBlock* Block_subBlock(Block* block, size_t size) {
    SubBlock* start;
    SubBlock* subblock;
    size_t check;
    size_t best;

    start = BLOCK_START(block);
    if (!start) {
        return NULL;
    }

    subblock = start;
    check = subblock->size_ & ~FLAGMASK;
    best = check;
    while (check < size) {
        subblock = subblock->next_;
        check = subblock->size_ & ~FLAGMASK;
        if (best < check) {
            best = check;
        }
        if (subblock == start) {
            block->max_size_ = best;
            return NULL;
        }
    }

    if ((check - size) >= 0x50) {
        SubBlock_split(subblock, size);
    }

    BLOCK_START(block) = subblock->next_;
    Block_unlink(block, subblock);
    return subblock;
}

static void Block_link(Block* block, SubBlock* subblock) {
    size_t size;
    SubBlock** sbptr;

    size = subblock->size_ & ~FLAGMASK;
    subblock->size_ &= ~2;
    ((SubBlock*)((char*)subblock + size))->size_ &= ~4;
    ((SubBlockTail*)((char*)subblock + size - sizeof(SubBlockTail)))->size_ = size;

    sbptr = &BLOCK_START(block);
    if (*sbptr) {
        subblock->prev_ = (*sbptr)->prev_;
        subblock->prev_->next_ = subblock;
        subblock->next_ = *sbptr;
        (*sbptr)->prev_ = subblock;
        *sbptr = subblock;
        *sbptr = SubBlock_merge_prev(*sbptr, sbptr);
        SubBlock_merge_next(*sbptr, sbptr);
    } else {
        *sbptr = subblock;
        subblock->prev_ = subblock;
        subblock->next_ = subblock;
    }

    if (block->max_size_ < ((*sbptr)->size_ & ~FLAGMASK)) {
        block->max_size_ = (*sbptr)->size_ & ~FLAGMASK;
    }
}

static void Block_unlink(Block* block, SubBlock* subblock) {
    size_t size;
    SubBlock** sbptr;

    size = subblock->size_ & ~FLAGMASK;
    subblock->size_ |= 2;
    ((SubBlock*)((char*)subblock + size))->size_ |= 4;

    sbptr = &BLOCK_START(block);
    if (*sbptr == subblock) {
        *sbptr = subblock->next_;
    }

    if (*sbptr == subblock) {
        *sbptr = NULL;
        block->max_size_ = 0;
    } else {
        subblock->next_->prev_ = subblock->prev_;
        subblock->prev_->next_ = subblock->next_;
    }
}

static void SubBlock_construct(
    SubBlock* subblock, size_t size, Block* block, int prevAllocated, int isAllocated
) {
    subblock->bp_ = (Block*)((long)block | 1);
    subblock->size_ = size;
    if (prevAllocated) {
        subblock->size_ |= 4;
    }
    if (isAllocated) {
        subblock->size_ |= 2;
        ((SubBlock*)(((long)subblock) + size))->size_ |= 4;
    } else {
        ((SubBlockTail*)(((long)subblock) + size - sizeof(SubBlockTail)))->size_ = size;
    }
}

static SubBlock* SubBlock_split(SubBlock* subblock, size_t pos) {
    size_t oldsize;
    int isfree;
    SubBlock* splitright;
    Block* block;

    oldsize = subblock->size_ & ~FLAGMASK;
    isfree = !(subblock->size_ & 2);
    splitright = (SubBlock*)((long)subblock + pos);
    block = (Block*)((long)subblock->bp_ & ~1);
    SubBlock_construct(subblock, pos, block, subblock->size_ & 4, !isfree);
    SubBlock_construct(splitright, oldsize - pos, block, !isfree, !isfree);
    if (isfree) {
        splitright->next_ = subblock->next_;
        splitright->next_->prev_ = splitright;
        splitright->prev_ = subblock;
        subblock->next_ = splitright;
    }

    return splitright;
}

static SubBlock* SubBlock_merge_prev(SubBlock* subblock, SubBlock** sbptr) {
    size_t prevsize;
    SubBlock* prevblock;

    if (!(subblock->size_ & 4)) {
        prevsize = ((SubBlockTail*)((char*)subblock - sizeof(SubBlockTail)))->size_;
        if (prevsize & 2) {
            return subblock;
        }

        prevblock = (SubBlock*)((char*)subblock - prevsize);
        prevblock->size_ &= FLAGMASK;
        prevblock->size_ |= (prevsize + (subblock->size_ & ~FLAGMASK)) & ~FLAGMASK;
        if (!(prevblock->size_ & 2)) {
            ((SubBlockTail*)((char*)prevblock + (prevsize + (subblock->size_ & ~FLAGMASK)) -
                             sizeof(SubBlockTail)))
                ->size_ = prevsize + (subblock->size_ & ~FLAGMASK);
        }

        if (*sbptr == subblock) {
            *sbptr = (*sbptr)->next_;
        }

        subblock->next_->prev_ = subblock->prev_;
        subblock->next_->prev_->next_ = subblock->next_;
        return prevblock;
    }

    return subblock;
}

static void SubBlock_merge_next(SubBlock* subblock, SubBlock** sbptr) {
    SubBlock* nextblock;
    size_t nextsize;

    nextblock = (SubBlock*)((char*)subblock + (subblock->size_ & ~FLAGMASK));
    if (!(nextblock->size_ & 2)) {
        nextsize = (subblock->size_ & ~FLAGMASK) + (nextblock->size_ & ~FLAGMASK);
        subblock->size_ &= FLAGMASK;
        subblock->size_ |= nextsize & ~FLAGMASK;

        if (!(subblock->size_ & 2)) {
            ((SubBlockTail*)((char*)subblock + nextsize - sizeof(SubBlockTail)))->size_ = nextsize;
        }

        if (!(subblock->size_ & 2)) {
            ((SubBlock*)((char*)subblock + nextsize))->size_ &= ~4;
        } else {
            ((SubBlock*)((char*)subblock + nextsize))->size_ |= 4;
        }

        if (*sbptr == nextblock) {
            *sbptr = (*sbptr)->next_;
        }
        if (*sbptr == nextblock) {
            *sbptr = NULL;
        }

        nextblock->next_->prev_ = nextblock->prev_;
        nextblock->prev_->next_ = nextblock->next_;
    }
}

static void link_block(Block* block) {
    if (start_) {
        block->prev_ = start_->prev_;
        block->prev_->next_ = block;
        block->next_ = start_;

        start_->prev_ = block;
        start_ = block;
    } else {
        start_ = block;
        block->prev_ = block;
        block->next_ = block;
    }
}

static Block* unlink_block(Block* block) {
    Block* newblock;

    newblock = block->next_;
    if (newblock == block) {
        newblock = NULL;
    }

    if (start_ == block) {
        start_ = newblock;
    }

    if (newblock) {
        newblock->prev_ = block->prev_;
        newblock->prev_->next_ = newblock;
    }

    block->next_ = NULL;
    block->prev_ = NULL;
    return newblock;
}

static Block* link_new_block(size_t size) {
    Block* block;

    size = (size + 0x1000 + sizeof(Block) + sizeof(BlockTail) - 1) & ~0xFFF;
    if (size < 0x10000) {
        size = 0x10000;
    }

    if (!(block = galloc(size))) {
        return NULL;
    }

    Block_construct(block, size);
    link_block(block);
    return block;
}

static void* allocate_from_var_pools(size_t size) {
    Block* block;
    SubBlock* subblock;

    size = (size + sizeof(Block) - 1) & ~FLAGMASK;
    if (size < 0x50) {
        size = 0x50;
    }

    block = start_ ? start_ : link_new_block(size);
    if (!block) {
        return NULL;
    }

    do {
        if (size <= block->max_size_) {
            if ((subblock = Block_subBlock(block, size))) {
                start_ = block;
                goto allocated;
            }
        }
    } while ((block = block->next_) != start_);

    block = link_new_block(size);
    if (!block) {
        return NULL;
    }
    subblock = Block_subBlock(block, size);
allocated:
    return (SubBlockBase*)subblock + 1;
}

#define Block_empty(block) \
    (_sb = (SubBlock*)((char*)(block) + sizeof(Block))), \
        !(_sb->size_ & 2) && \
        (_sb->size_ & ~FLAGMASK) == \
            ((block)->size_ & ~FLAGMASK) - sizeof(Block) - sizeof(BlockTail)

static void deallocate_from_var_pools(void* buf) {
    SubBlock* subblock = VARBUF_SB(buf);
    SubBlock* _sb;
    Block* block = SBB_BLOCK(subblock);

    Block_link(block, subblock);
    if (Block_empty(block)) {
        unlink_block(block);
    }
}

static void FixBlock_construct(
    FixBlock* fixblock,
    FixBlock* prev,
    FixBlock* next,
    int poolIndex,
    void* buffer,
    size_t buffersize
) {
    size_t entrysize;
    size_t entrycount;
    char* fsb;
    size_t i;
    char* fsbnext;

    fixblock->reserve_ = prev;
    fixblock->next_ = next;
    fixblock->client_size_ = fix_pool_sizes[poolIndex];

    entrysize = fix_pool_sizes[poolIndex] + sizeof(void*);
    entrycount = (buffersize / entrysize);
    fsb = buffer;
    for (i = 0; i < (entrycount - 1); i++) {
        fsbnext = fsb + entrysize;
        ((FixSubBlock*)fsb)->block_ = fixblock;
        ((FixSubBlock*)fsb)->next_ = (FixSubBlock*)fsbnext;
        fsb = fsbnext;
    }

    ((FixSubBlock*)fsb)->block_ = fixblock;
    ((FixSubBlock*)fsb)->next_ = fix_start[poolIndex].start_;
    fix_start[poolIndex].start_ = buffer;
}

static void* allocate_from_fixed_pools(size_t size) {
    int poolIndex;
    FixSubBlock* fsb;

    poolIndex = 0;
    while (size > fix_pool_sizes[poolIndex]) {
        poolIndex++;
    }

    if (!fix_start[poolIndex].start_) {
        char* buf;
        size_t bufsize;

        buf = allocate_from_var_pools(4000);
        if (!buf) {
            return NULL;
        }

        bufsize = !BUF_IS_VAR(buf) ? FIXBUF_SIZE(buf) : VARBUF_SIZE(buf);
        FixBlock_construct(
            (FixBlock*)buf,
            NULL,
            fix_start[poolIndex].next_,
            poolIndex,
            buf + sizeof(FixBlock),
            bufsize - sizeof(FixBlock)
        );
        fix_start[poolIndex].next_ = (FixBlock*)buf;
    }

    fsb = fix_start[poolIndex].start_;
    fix_start[poolIndex].start_ = fsb->next_;
    fix_start[poolIndex].n_allocated_++;
    return ((FixSubBlockBase*)fsb) + 1;
}

static void deallocate_from_fixed_pools(void* buf, size_t size) {
    int poolIndex;
    FixBlock* fixblock;
    FixBlock* nextfb;
    FixSubBlock* fsb;

    poolIndex = 0;
    while (size > fix_pool_sizes[poolIndex]) {
        poolIndex++;
    }

    fsb = (FixSubBlock*)((long)buf - sizeof(FixSubBlockBase));
    fsb->next_ = fix_start[poolIndex].start_;
    fix_start[poolIndex].start_ = fsb;
    if (--fix_start[poolIndex].n_allocated_ == 0) {
        fixblock = fix_start[poolIndex].next_;
        while (fixblock) {
            nextfb = fixblock->next_;
            deallocate_from_var_pools(fixblock);
            fixblock = nextfb;
        }
        fix_start[poolIndex].next_ = NULL;
        fix_start[poolIndex].start_ = NULL;
    }
}

size_t IRO_msize(void* buf) {
    return BUF_SIZE(buf);
}

void* IRO_malloc(size_t size) {
    void* result;

    if (!size) {
        return NULL;
    }

    if (size <= 0x44) {
        result = allocate_from_fixed_pools(size);
    } else {
        result = allocate_from_var_pools(size);
    }
    return result;
}

void IRO_free(void* buf) {
    size_t size;

    if (!buf) {
        return;
    }

    size = !BUF_IS_VAR(buf) ? FIXBUF_SIZE(buf) : VARBUF_SIZE(buf);
    if (size <= 0x44) {
        deallocate_from_fixed_pools(buf, size);
    } else {
        deallocate_from_var_pools(buf);
    }
}

void* IRO_realloc(void* buf, size_t newsize) {
    size_t oldsize;
    size_t newblocksize;
    void* newbuf;

    if (!buf) {
        return IRO_malloc(newsize);
    }

    if (!newsize) {
        IRO_free(buf);
        return NULL;
    }

    oldsize = !BUF_IS_VAR(buf) ? FIXBUF_SIZE(buf) : VARBUF_SIZE(buf);
    if (newsize > oldsize) {
        if (BUF_IS_VAR(buf)) {
            newblocksize = (newsize + sizeof(Block) - 1) & ~FLAGMASK;
            if (newblocksize < 0x50) {
                newblocksize = 0x50;
            }
            SubBlock_merge_next(VARBUF_SB(buf), &BLOCK_START(VARBUF_BLOCK(buf)));
            if (VARBUF_BLOCKSIZE(buf) >= newblocksize) {
                if ((VARBUF_BLOCKSIZE(buf) - newblocksize) >= 0x50) {
                    Block_link(VARBUF_BLOCK(buf), SubBlock_split(VARBUF_SB(buf), newblocksize));
                }
                return buf;
            }
        }

        newbuf = IRO_malloc(newsize);
        if (!newbuf) {
            return NULL;
        }

        memcpy(newbuf, buf, oldsize);
        IRO_free(buf);
        return newbuf;
    }

    if (BUF_IS_VAR(buf)) {
        newsize = (newsize + sizeof(Block) - 1) & ~FLAGMASK;
        if (newsize < 0x50) {
            newsize = 0x50;
        }
        if ((VARBUF_BLOCKSIZE(buf) - newsize) >= 0x50) {
            Block_link(VARBUF_BLOCK(buf), SubBlock_split(VARBUF_SB(buf), newsize));
        }
    }

    return buf;
}

void* IRO_calloc(size_t nmemb, size_t size) {
    void* buf;
    size_t len = size * nmemb;
    buf = IRO_malloc(len);
    if (buf) {
        memset(buf, 0, len);
    }
    return buf;
}

void IRO_pool_free_all(void) {
    int i;
    Block* block;
    if ((block = start_)) {
        do {
            block = block->next_;
        } while (block != start_);
        start_ = NULL;

        for (i = 0; i < 4; i++) {
            fix_start[i].next_ = NULL;
            fix_start[i].start_ = NULL;
            fix_start[i].n_allocated_ = 0;
        }
    }
}

void IRO_InitializeAllocator(void) {
    if (!initialized) {
        int i;
        for (i = 0; i < 4; i++) {
            fix_start[i].next_ = NULL;
            fix_start[i].start_ = NULL;
            fix_start[i].n_allocated_ = 0;
        }
        start_ = NULL;
        initialized = TRUE;
    }
}

void IRO_TerminateAllocator(void) {
    initialized = FALSE;
}
