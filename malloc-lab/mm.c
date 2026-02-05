/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4     /* Word and header/footer size(bytes) */
#define DSIZE 8     /* Double word size(bytes) */
#define NUM_CLASS 24        /* free list의 개수 */
#define CHUNKSIZE (1 << 12)     /* Extend heap by this amount (bytes) */
#define SUCC(bp) (*(void **)((bp) + DSIZE))            /* 다음 가용 리스트의 포인터 */
#define PRED(bp) (*(void **)(bp))                    /* 이전 가용 리스트의 포인터 */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size ans allocated fields from address */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static void *heap_listp = 0;        /* 항상 프롤로그 bp를 가르킴 */
static void *seg_free_lists[NUM_CLASS];     /* free list의 배열 */

static int list_index(size_t size);
static void *find_list(size_t asize) ;
static void add_node(void *ptr);
static void remove_node(void *ptr);

static void *coalesce(void *bp);
static void *extend_heap (size_t words);
static void *find_fit(size_t size);
static void place(void* bp, size_t asize);


/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    for(int i = 0; i < NUM_CLASS; i++) {
        seg_free_lists[i] = NULL;
    }

    if((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) - 1)
        return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));          /* prologue */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));          /* prologue */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));          /* Epilogue */
    heap_listp += (2 * WSIZE);

    if(extend_heap(CHUNKSIZE/WSIZE) == NULL) {
        return -1;
    }

    return 0;
}

static void *extend_heap (size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(size, 1));

    return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *ptr;

    if(size == 0) {
        return NULL;
    }

    /* minimum 16bytes (4words) */
    if (size <= DSIZE) {
        asize = 2 * DSIZE;
    }
    else {
        asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);     /* 8의 배수 올림 */
    }

    /* search the free list for a fit */
    if ((ptr = find_fit(asize)) != NULL) {
        place(ptr, asize);
        return ptr;
    }

    /* No fit found */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((ptr = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL ;
    }
    place(ptr, asize);
    return ptr;
}
/* first-fit */
static void *find_fit(size_t size) {
    for(int idx = list_index(size); idx < NUM_CLASS; idx++) {
        for(void *bp = seg_free_lists[idx]; bp; bp = SUCC(bp)){
            if(!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= size){
                return bp;
            }
        }
    }
    return NULL;
}

static void place(void* bp, size_t asize) {
    size_t curr_size = GET_SIZE(HDRP(bp));

    remove_node(bp);
    if (curr_size - asize >= 3 * DSIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *nptr = NEXT_BLKP(bp);
        PUT(HDRP(nptr), PACK((curr_size - asize), 0));
        PUT(FTRP(nptr), PACK((curr_size - asize), 0));
        add_node(nptr);
    }
    else {
        PUT(HDRP(bp), PACK(curr_size, 1));
        PUT(FTRP(bp), PACK(curr_size, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));              /* Header 0 */
    PUT(FTRP(ptr), PACK(size, 0));              /* Footer 0 */
    coalesce(ptr); 
}

static void *coalesce(void *ptr) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    if(PREV_BLKP(ptr) == heap_listp) {
        prev_alloc = 1;
    }
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    /* Case 1 : prev and next not free, 병합할 게 없음. */
    if (prev_alloc && next_alloc) {
    }
    /* Case 2 : prev not free and next is free */
    else if (prev_alloc && !next_alloc) {
        remove_node(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }
    /* Case 3 : prev is free and next is not free */
    else if (!prev_alloc && next_alloc){
        remove_node(PREV_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size , 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    /* Case 4 : prev and next is free */
    else {
        remove_node(PREV_BLKP(ptr));
        remove_node(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));       /* new header */
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));       /* new footer */
        ptr = PREV_BLKP(ptr);
    }

    add_node(ptr);
    return ptr;
}


/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    
    size_t originsize = GET_SIZE(HDRP(oldptr));
    size_t new_size = size + DSIZE;

    /* 기존의 것이 크기 떄문에 궅이 줄일 필요 없음 */
    if (new_size <= originsize) {
        return oldptr;
    }
    else {
        size_t addSize = originsize + GET_SIZE(HDRP(NEXT_BLKP(oldptr)));
        if(!GET_ALLOC(HDRP(NEXT_BLKP(oldptr))) && new_size <= addSize) {
            remove_node(NEXT_BLKP(oldptr));
            PUT(HDRP(oldptr), PACK(addSize, 1));
            PUT(FTRP(oldptr), PACK(addSize, 1));
            return oldptr;
        }
        else {
            newptr = mm_malloc(new_size);
            if (newptr == NULL) {
                return NULL;
            }
            size_t old_payload = GET_SIZE(HDRP(oldptr)) - DSIZE;
            size_t new_payload = GET_SIZE(HDRP(newptr)) - DSIZE;
            size_t copysize   = size;
            if (copysize > old_payload) copysize = old_payload;
            if (copysize > new_payload) copysize = new_payload;
            memcpy(newptr, oldptr, copysize);
            mm_free(oldptr);
            return newptr;
        }
    }
    return NULL;
}

/* LIFO */
static void add_node(void *ptr) {
    size_t asize = GET_SIZE(HDRP(ptr));
    int idx = list_index(asize);
    void *head = seg_free_lists[idx];

    PRED(ptr) = NULL;
    SUCC(ptr) = head;

    if(head != NULL){
        PRED(head) = ptr;
    }
    seg_free_lists[idx] = ptr;
}

static void remove_node(void *ptr) {
    size_t asize = GET_SIZE(HDRP(ptr));
    int idx = list_index(asize);
    void *pred = PRED(ptr);
    void *succ = SUCC(ptr);

    if(pred) SUCC(pred) = succ;
    else seg_free_lists[idx] = succ;

    if(succ) PRED(succ) = pred;
    
    PRED(ptr) = SUCC(ptr) = NULL;
}

static int list_index(size_t asize) {
    int idx = 0;
    while(idx < NUM_CLASS - 1 && (asize >>= 1)) idx++;
    return idx;
}

static void *find_list(size_t asize) {
    for(int i = list_index(asize); i < NUM_CLASS; i++) {
        for (void *bp = seg_free_lists[i]; bp != NULL; bp = SUCC(bp)) {
            if (GET_SIZE(HDRP(bp)) >= asize)    return bp;
        }
    }
    return NULL;
}