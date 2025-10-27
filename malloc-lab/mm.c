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


static char *free_list_head = 0;        /* free list start */
static void insert_node(void *ptr);
static void remove_node(void *ptr);

static char *heap_listp = 0; /* always point to prologue block */
static void *coalesce(void *ptr);
static void *extend_heap(size_t words);
static void *find_fit(size_t size);
static void place(void *ptr, size_t size);

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
#define CHUNKSIZE (1 << 12)     /* Extend heap by this amount (bytes) */
#define SUCC(bp) (*(char **)(bp + sizeof(void *)))
#define PRED(bp) (*(char **)(bp))

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

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) - 1) {
        return -1;
    }
    PUT(heap_listp, 0);                                     /* Alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));          /* prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));          /* prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));              /* epilogue header */
    heap_listp += (2 * WSIZE);                              /* always point to prologue block */
    free_list_head = NULL;

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;       /* 인접한 2워드의 배수로 반올림 */
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   /* New epilogue heaper */ 

    return coalesce(bp);   /* colaesce previous free block */
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

/* BEST_fit*/
static void *find_fit(size_t size) {
    void *ptr;
    ptr = free_list_head;

    size_t min_remain_size = (size_t) - 1;
    void *min_ptr = NULL;

    for(; ptr != NULL; ptr = SUCC(ptr)) {
        size_t curr_size = GET_SIZE(HDRP(ptr));
        if (curr_size < size) {         /* 음수로 바뀌면 overflow가 발생해서 아주 큰 양수로 변함. 그래서 이상한 값이 들어감. 무조건 이 조건을 붙여줘야함. */
            continue;
        }
        size_t remain_size = curr_size - size;
        if (remain_size == 0){
            return ptr;
        }
        if (min_remain_size > remain_size) {
            min_remain_size = remain_size;
            min_ptr = ptr;
        }
    }
    
    return min_ptr;
}

static void place(void *ptr, size_t size) {
    size_t curr_size = GET_SIZE(HDRP(ptr));
    size_t MINFREE = ALIGN(WSIZE + WSIZE + 2 * sizeof(void *));
    remove_node(ptr);

    /* 최소 블럭 크기와 같거나 큰 경우. 사이즈 분활해야 함.*/
    if((curr_size - size) >= MINFREE) {
        size_t remain_size = curr_size - size;
        PUT(HDRP(ptr), PACK(size, 1));
        PUT(FTRP(ptr), PACK(size, 1));
        void *next_ptr = NEXT_BLKP(ptr);
        PUT(HDRP(next_ptr), PACK(remain_size, 0));
        PUT(FTRP(next_ptr), PACK(remain_size, 0));
        insert_node(next_ptr);
    }
    /* 작은 경우 그냥 블럭 전체를 사용한다고 선언하면 됨. */
    else {
        PUT(HDRP(ptr), PACK(curr_size, 1));
        PUT(FTRP(ptr), PACK(curr_size, 1));
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
    coalesce(ptr);                              /* 연결 */
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

    insert_node(ptr);
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
static void insert_node(void *ptr) {
    SUCC(ptr) = free_list_head;
    PRED(ptr) = NULL;
    if (free_list_head != NULL) {
        PRED(free_list_head) = ptr;
    }
    free_list_head = ptr;
}

static void remove_node(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    void *pred;
    void *succ;
    
    pred=PRED(ptr); succ=SUCC(ptr);
    if (pred) SUCC(pred)=succ; else free_list_head=succ;
    if (succ) PRED(succ)=pred;
    PRED(ptr)=SUCC(ptr)=NULL;
}