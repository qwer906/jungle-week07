/*
 * Segregated Free List Allocator (size-sorted insert + back-split + realloc-tag)
 * - 8B alignment, boundary tags, immediate coalescing
 * - size-classed segregated explicit free lists (doubly linked)
 * - back-split for large requests, realloc in-place first with tag optimization
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * Team info (template)
 ********************************************************/
team_t team = {
    "ateam",
    "Harry Bovik",
    "bovik@cs.cmu.edu",
    "",
    ""
};

/* ===== Alignment / Sizes ===== */
#define ALIGNMENT 8
#define ALIGN(size)      (((size) + (ALIGNMENT - 1)) & ~0x7)

#define WSIZE 4                  /* header/footer size (bytes) */
#define DSIZE 8                  /* double word size (bytes) */
#define CHUNKSIZE (1 << 12)      /* heap extend size (bytes) */
#define NUM_CLASS 24             /* number of segregated classes */

/* Explicit free list needs pred/succ (16B) + hdr/ftr (8B) = 24B minimum */
#define MIN_BLK (3 * DSIZE)      /* 24 bytes */

/* ===== Word pack & access ===== */
#define PACK(size, alloc)  ((size) | (alloc))          /* alloc bit in LSB, tag uses bit 1 */
#define GET(p)             (*(unsigned int *)(p))
#define PUT(p, val)        (*(unsigned int *)(p) = (val))

#define GET_SIZE(p)        (GET(p) & ~0x7)
#define GET_ALLOC(p)       (GET(p) & 0x1)

/* ===== Realloc Tag (bit 1) ===== */
#define GET_TAG(p)         (GET(p) & 0x2)
#define SET_RATAG(p)       (PUT((p), GET(p) | 0x2))
#define REMOVE_RATAG(p)    (PUT((p), GET(p) & ~0x2))

/* ===== Block pointer math ===== */
#define HDRP(bp)           ((char *)(bp) - WSIZE)
#define FTRP(bp)           ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp)      ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)      ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* ===== Explicit free-list payload layout: [hdr][pred(8)][succ(8)]...[ftr] ===== */
#define PRED_PTR(bp)       ((char *)(bp))            /* address of pred field  */
#define SUCC_PTR(bp)       ((char *)(bp) + DSIZE)    /* address of succ field  */
#define PRED(bp)           (*(void **)(PRED_PTR(bp)))
#define SUCC(bp)           (*(void **)(SUCC_PTR(bp)))

#define MAX(x,y)           ((x) > (y) ? (x) : (y))
#define REALLOC_BUFFER     (1 << 7)                  /* 128B buffer for realloc growth */

/* ===== Globals ===== */
static void *heap_listp = NULL;                /* points to prologue payload */
static void *seg_free_lists[NUM_CLASS];        /* segregated free list heads */

/* ===== Internal helpers ===== */
static int   list_index(size_t asize);
static void  add_node(void *bp);               /* size-sorted insert */
static void  remove_node(void *bp);
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void  place(void *bp, size_t asize);

/* ======================================================
 * mm_init - initialize the malloc package
 * ====================================================== */
int mm_init(void)
{
    for (int i = 0; i < NUM_CLASS; i++) seg_free_lists[i] = NULL;

    /* prologue (8B alloc) + epilogue (0B alloc) */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) return -1;
    PUT(heap_listp, 0);                             /* alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));  /* prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));  /* prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));      /* epilogue header */
    heap_listp += (2 * WSIZE);                      /* point to prologue payload */

    /* initial heap extension */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) return -1;
    return 0;
}

/* ======================================================
 * extend_heap - extend heap by 'words' (WSIZE units)
 * ====================================================== */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; /* keep 8B alignment */

    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    /* new free block */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* NEW epilogue (MUST be size=0) */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    /* coalesce with prior if free, then add to list */
    return coalesce(bp);
}

/* ======================================================
 * list_index - size to class
 * ====================================================== */
static int list_index(size_t asize)
{
    int idx = 0;
    /* power-of-two-ish bucketing */
    while (idx < NUM_CLASS - 1 && (asize >>= 1)) idx++;
    return idx;
}

/* ======================================================
 * add_node - size-sorted insert into class list
 * ====================================================== */
static void add_node(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    int idx = list_index(size);

    void *search = seg_free_lists[idx];
    void *insert = NULL;

    /* size ascending order */
    while (search && size > GET_SIZE(HDRP(search))) {
        insert = search;
        search = SUCC(search);
    }

    if (search != NULL) {
        if (insert != NULL) {
            /* middle */
            SUCC(bp) = search;
            PRED(bp) = insert;
            PRED(search) = bp;
            SUCC(insert) = bp;
        } else {
            /* at head */
            SUCC(bp) = search;
            PRED(bp) = NULL;
            PRED(search) = bp;
            seg_free_lists[idx] = bp;
        }
    } else {
        if (insert != NULL) {
            /* at tail */
            SUCC(bp) = NULL;
            PRED(bp) = insert;
            SUCC(insert) = bp;
        } else {
            /* empty list */
            SUCC(bp) = NULL;
            PRED(bp) = NULL;
            seg_free_lists[idx] = bp;
        }
    }
}

/* ======================================================
 * remove_node - unlink bp from its class list
 * ====================================================== */
static void remove_node(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    int idx = list_index(size);

    void *pred = PRED(bp);
    void *succ = SUCC(bp);

    if (pred) SUCC(pred) = succ;
    else      seg_free_lists[idx] = succ;

    if (succ) PRED(succ) = pred;

    PRED(bp) = SUCC(bp) = NULL;
}

/* ======================================================
 * coalesce - immediate coalescing with boundary tags
 * (realloc tag on prev prevents coalescing)
 * ====================================================== */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* If prev has realloc tag, forbid coalescing backward */
    if (GET_TAG(HDRP(PREV_BLKP(bp)))) prev_alloc = 1;

    if (prev_alloc && next_alloc) {
        /* Case 1: no coalesce */
    }
    else if (prev_alloc && !next_alloc) {
        /* Case 2: merge with next */
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {
        /* Case 3: merge with prev */
        remove_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else {
        /* Case 4: merge both sides */
        remove_node(PREV_BLKP(bp));
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    add_node(bp);
    return bp;
}

/* ======================================================
 * find_fit - first-fit within appropriate classes
 * ====================================================== */
static void *find_fit(size_t asize)
{
    for (int idx = list_index(asize); idx < NUM_CLASS; idx++) {
        for (void *bp = seg_free_lists[idx]; bp != NULL; bp = SUCC(bp)) {
            if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize) {
                return bp;
            }
        }
    }
    return NULL;
}

/* ======================================================
 * place - allocate at bp; split if remainder >= MIN_BLK
 * Back-split for larger requests (asize >= 100B)
 * ====================================================== */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t remainder = csize - asize;

    remove_node(bp);

    if (remainder < MIN_BLK) {
        /* too small to split */
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
    else if (asize >= 100) {
        /* back-split: leave front free, allocate tail */
        PUT(HDRP(bp), PACK(remainder, 0));
        PUT(FTRP(bp), PACK(remainder, 0));
        void *nbp = NEXT_BLKP(bp);
        PUT(HDRP(nbp), PACK(asize, 1));
        PUT(FTRP(nbp), PACK(asize, 1));
        add_node(bp);
        /* return nbp to caller via mm_malloc; caller already has ptr */
    } else {
        /* normal: allocate front, leave tail free */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *nbp = NEXT_BLKP(bp);
        PUT(HDRP(nbp), PACK(remainder, 0));
        PUT(FTRP(nbp), PACK(remainder, 0));
        add_node(nbp);
    }
}

/* ======================================================
 * mm_malloc - allocate a block with at least 'size' bytes
 * ====================================================== */
void *mm_malloc(size_t size)
{
    if (size == 0) return NULL;

    size_t asize;      /* adjusted size incl. hdr/ftr & alignment */
    size_t extendsize;
    void *bp = NULL;

    /* ensure MIN_BLK and 8B alignment */
    size_t need = size + DSIZE; /* payload + hdr/ftr(8B total) -> DSIZE added works with rounding */
    asize = DSIZE * ((need + (DSIZE - 1)) / DSIZE);
    if (asize < MIN_BLK) asize = MIN_BLK;

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        /* place() may back-split; if so, allocated block is at NEXT_BLKP of original */
        if (GET_ALLOC(HDRP(bp)) == 0) bp = NEXT_BLKP(bp);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) return NULL;
    place(bp, asize);
    if (GET_ALLOC(HDRP(bp)) == 0) bp = NEXT_BLKP(bp);
    return bp;
}

/* ======================================================
 * mm_free - free and coalesce
 * ====================================================== */
void mm_free(void *bp)
{
    if (bp == NULL) return;

    size_t size = GET_SIZE(HDRP(bp));

    /* allow next coalescing again (clear realloc tag on next) */
    REMOVE_RATAG(HDRP(NEXT_BLKP(bp)));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/* ======================================================
 * mm_realloc - in-place first, else new malloc+copy
 * uses REALLOC_BUFFER & realloc tag on next
 * ====================================================== */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0)   { mm_free(ptr); return NULL; }

    size_t curr = GET_SIZE(HDRP(ptr));

    /* compute target size like mm_malloc, then add buffer */
    size_t need = size + DSIZE;
    size_t req  = DSIZE * ((need + (DSIZE - 1)) / DSIZE);
    if (req < MIN_BLK) req = MIN_BLK;
    req += REALLOC_BUFFER;

    if (req <= curr) {
        /* already big enough */
        return ptr;
    }

    /* try to grow into next free (or epilogue) */
    void *next = NEXT_BLKP(ptr);
    size_t next_size = GET_SIZE(HDRP(next));
    int next_free = !GET_ALLOC(HDRP(next)) || (next_size == 0);

    if (next_free) {
        size_t combined = curr + next_size;
        if (combined >= req) {
            if (next_size != 0) remove_node(next);  /* if real free block */
            PUT(HDRP(ptr), PACK(combined, 1));
            PUT(FTRP(ptr), PACK(combined, 1));
            return ptr;
        }
    }

    /* fallback: new alloc + copy */
    void *newp = mm_malloc(size);
    if (!newp) return NULL;

    size_t old_payload = curr - DSIZE;
    size_t new_payload = GET_SIZE(HDRP(newp)) - DSIZE;
    size_t csz = size;
    if (csz > old_payload) csz = old_payload;
    if (csz > new_payload) csz = new_payload;
    memcpy(newp, ptr, csz);
    mm_free(ptr);

    /* mark next with realloc tag to improve subsequent growth */
    void *after = NEXT_BLKP(newp);
    if (GET_SIZE(HDRP(after)) > 0) SET_RATAG(HDRP(after));

    return newp;
}