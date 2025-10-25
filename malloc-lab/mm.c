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
    " ",
    /* Second member's email address (leave blank if none) */
    " "};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4     /* Word and header/footer size(bytes) */
#define DSIZE 8     /* Double word size(bytes) */
#define CHUNKSIZE (1 << 12)     /* Extend heap by this amount (bytes) */

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
#define HDPR(bp) ((char *)(bp) - WSIZE)
#define FTPR(bp) ((char *)(bp) + GET_SIZE(HDPR(bp)) - DSIZE)

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

    PUT(HDPR(bp), PACK(size, 0));
    PUT(FTPR(bp), PACK(size, 0));
    PUT(HDPR(NEXT_BLKP(bp)), PACK(0, 1));   /* New epilogue heaper */ 

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

static void *find_fit(size_t size) {
    char *ptr;
    ptr = NEXT_BLKP(heap_listp);

    while (1) {
        /* break condition epilogue size is 0. */
        if (GET_SIZE(HDPR(ptr)) == 0) {
            break;
        }
        /* first fit : big or same && GET_ALLOC is 0 */
        if (GET_SIZE(HDPR(ptr)) >= size && !GET_ALLOC(HDPR(ptr))) {
            return ptr;
        }
        ptr = NEXT_BLKP(ptr);
    }
    /* no fit */
    return NULL;
}

static void place(void *ptr, size_t size) {
    size_t curr_size = GET_SIZE(HDPR(ptr));

    /* 최소 블럭 크기와 같거나 큰 경우. 사이즈 분활해야 함.*/
    if((curr_size - size) >= 2 * DSIZE) {
        size_t remain_size = curr_size - size;
        PUT(HDPR(ptr), PACK(size, 1));
        PUT(FTPR(ptr), PACK(size, 1));
        ptr = NEXT_BLKP(ptr);
        PUT(HDPR(ptr), PACK(remain_size, 0));
        PUT(FTPR(ptr), PACK(remain_size, 0));
    }
    /* 작은 경우 그냥 블럭 전체를 사용한다고 선언하면 됨. */
    else {
        PUT(HDPR(ptr), PACK(curr_size, 1));
        PUT(FTPR(ptr), PACK(curr_size, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDPR(ptr));

    PUT(HDPR(ptr), PACK(size, 0));              /* Header 0 */
    PUT(FTPR(ptr), PACK(size, 0));              /* Footer 0 */
    coalesce(ptr);                              /* 연결 */
}

static void *coalesce(void *ptr) {
    size_t prev_alloc = GET_ALLOC(FTPR(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDPR(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDPR(ptr));

    /* Case 1 : prev and next not free */
    if (prev_alloc && next_alloc) {
        return ptr;
    }
    /* Case 2 : prev not free and next is free */
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDPR(NEXT_BLKP(ptr)));
        PUT(HDPR(ptr), PACK(size, 0));
        PUT(FTPR(ptr), PACK(size, 0));
    }
    /* Case 3 : prev is free and next is not free */
    else if (!prev_alloc && next_alloc){
        size += GET_SIZE(HDPR(PREV_BLKP(ptr)));
        PUT(FTPR(ptr), PACK(size , 0));
        PUT(HDPR(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    /* Case 4 : prev and next is free */
    else {
        size += GET_SIZE(HDPR(PREV_BLKP(ptr))) + GET_SIZE(HDPR(NEXT_BLKP(ptr)));
        PUT(HDPR(PREV_BLKP(ptr)), PACK(size, 0));       /* new header */
        PUT(FTPR(NEXT_BLKP(ptr)), PACK(size, 0));       /* new footer */
        ptr = PREV_BLKP(ptr);
    }

    return ptr;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    // void *oldptr = ptr;
    // void *newptr;
    // size_t copySize;

    // newptr = mm_malloc(size);
    // if (newptr == NULL)
    //     return NULL;
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    // if (size < copySize)
    //     copySize = size;
    // memcpy(newptr, oldptr, copySize);
    // mm_free(oldptr);
    // return newptr;

    if (ptr == NULL) {
        return mm_malloc(size);
    }
    if (size == 0) {
        mm_free(ptr);
        return ptr;
    }

    size_t asize = size;
    if (asize <= DSIZE) {
        asize = 2 * DSIZE;
    }
    else {
        asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);
    }

    size_t old_size = GET_SIZE(HDPR(ptr));
    size_t old_payplad = old_size - DSIZE;

    /* shrink case */
    if (old_size >= asize) {
        size_t remain_size = old_size - asize;
        if (remain_size >= 2 * DSIZE) {
            PUT(HDPR(ptr), PACK(asize, 1));
            PUT(FTPR(ptr), PACK(asize, 1));
            char *next_ptr = NEXT_BLKP(ptr);
            PUT(HDPR(next_ptr), PACK(remain_size, 0));
            PUT(FTPR(next_ptr), PACK(remain_size, 0));
            coalesce(next_ptr);
        }
        return ptr;
    }
    /* grow case */
    else {
        size_t need = asize - old_size;
        char *next_ptr = NEXT_BLKP(ptr);
        size_t next_alloc = GET_ALLOC(HDPR(next_ptr));
        size_t next_size = GET_SIZE(HDPR(next_ptr));
        size_t new_size = old_size + next_size;

        /* next is free and new_size is bigger than asize*/
        if (!next_alloc && new_size >= asize){
            size_t remain_size = new_size - asize;
            /* remain size is bigger than 2*DSIZE*/
            if  (remain_size >= 2 * DSIZE) {
                PUT(HDPR(ptr), PACK(asize, 1));
                PUT(FTPR(ptr), PACK(asize, 1));
                char *next_ptr = NEXT_BLKP(ptr);
                PUT(HDPR(next_ptr), PACK(remain_size, 0));
                PUT(FTPR(next_ptr), PACK(remain_size, 0));
                coalesce(next_ptr);
            }
            /* remain size is smaller than 2*DSIZE */
            else {
                PUT(HDPR(ptr), PACK(new_size, 1));
                PUT(FTPR(ptr), PACK(new_size, 1));
            }
            return ptr;
        }
        /* next is not free or new_size is smaller than asize*/
        else if (next_alloc || (new_size < asize)){
            void *newp = mm_malloc(size); /* new block ptr */
            if (newp == NULL) {
                return NULL ;
            }
            /* 기존의 데이터를 얼마나 복사를 할까를 결정을 하는 것 / 더 작은 수를 복사를 해야 복사 가능하고 segment가 안 남.
               새로 요청한 (사용자가 직접 위의 size) 크기가 더 크면 기존의 데이터의 크기를 다 담고도 메모리 공간이 남아돔.
               하지만 요청한 size가 데이터의 크기 보다 작으면 사용자가 작게 줄여도 된다는 뜻이기 때문에 새로 할당한 메모리에 데이터의 앞부분을 쓰고 뒷부분은 초기화되지 않은 메모리에 작성.*/
            size_t copy = (size < old_payplad) ? size : old_payplad;
            memcpy(newp, ptr, copy);
            mm_free(ptr);
            return newp;
        }
    }
}