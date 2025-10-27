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

static char *heap_listp = 0;
static char *last = 0;      /* 지난 번에 멈춘 곳부터 탐색 시작 */
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

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = val)

#define GET_SIZE(p) (GET(p) & (~0x07))
#define GET_ALLOC(p) (GET(p) & (0x01))

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) - 1) {
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));      /* Prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));      /* Prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));          /* Epilogue header */
    heap_listp += 2 * WSIZE;
    last = heap_listp;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }
    return 0;
}

static void *extend_heap(size_t words) {
    char *ptr;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long) (ptr = mem_sbrk(size)) == -1) {
        return NULL;
    }

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1));

    return coalesce(ptr);
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

    if (size == 0) {
        return NULL;
    }

    if (size <= DSIZE) {
        asize = 2 * DSIZE;
    }
    else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }

    if ((ptr = find_fit(asize)) != NULL) {
        place(ptr, asize);
        return ptr;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((ptr = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(ptr, asize);
    return ptr;
}

/* next fit */
static void *find_fit(size_t size) {
    if (last == NULL) {
        last = (char *)mem_heap_lo() + 2 * WSIZE;     /* heap 시작 주소에서 2워드 (프롤로그 뺀 것) */
    }
    char *ptr = last;

    /* 1차 현재 부터 epilogue까지 */
    for (; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if (GET_SIZE(HDRP(ptr)) >= size && !GET_ALLOC(HDRP(ptr))) {
            last = ptr;
            return ptr;
        }
    }
    for(ptr = NEXT_BLKP(heap_listp); ptr != last && GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if (GET_SIZE(HDRP(ptr)) >= size && !GET_ALLOC(HDRP(ptr))) {
            last = ptr;
            return ptr;
        }
    }
    return NULL;
}

static void place(void *ptr, size_t size) {
    size_t curr_size = GET_SIZE(HDRP(ptr));
    size_t remain_size = curr_size - size;

    if (remain_size >= 2 * DSIZE) {
        PUT(HDRP(ptr), PACK(size, 1));
        PUT(FTRP(ptr), PACK(size, 1));
        void *next_ptr = NEXT_BLKP(ptr);
        PUT(HDRP(next_ptr), PACK(remain_size, 0));
        PUT(FTRP(next_ptr), PACK(remain_size, 0));
    }
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

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

static void *coalesce(void *ptr) {
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    /* case 1 : Both are not free */
    if (prev_alloc && next_alloc) {
        return ptr;
    }
    /* case 2 : next block is free */
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }
    /* case 3 : prev block is free */
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    /* Both are free */
    else {
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr))) + GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    last = ptr;
    return ptr;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }

    // 1) 정렬된 요청 블록 크기(asize): (payload + header/footer)
    size_t asize = (size <= DSIZE)
        ? 2*DSIZE
        : DSIZE * ((size + DSIZE + (DSIZE-1)) / DSIZE);

    // 2) 현재 블록 총 크기를 '헤더에서' 즉시 읽어 고정
    unsigned int chdr = GET(HDRP(ptr));
    size_t curr_size = chdr & ~0x7;   // block size
    assert((curr_size % DSIZE) == 0);

    // ---------- shrink ----------
    if (curr_size >= asize) {
        size_t remain = curr_size - asize;
        if (remain >= 2*DSIZE) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            char *rem = (char*)ptr + asize;
            PUT(HDRP(rem), PACK(remain, 0));
            PUT(FTRP(rem), PACK(remain, 0));
            coalesce(rem);
        }
        return ptr;
    }

    // ---------- grow: 뒤 블록 검사 (한 번만 계산) ----------
    char *next = (char*)ptr + curr_size;
    unsigned int nhdr = GET(HDRP(next));
    size_t next_size  = nhdr & ~0x7;
    int    next_alloc = nhdr & 0x1;

    // A) 뒤가 free이고 합치면 충분
    if (!next_alloc && curr_size + next_size >= asize) {
        // next의 기존 푸터가 힙 경계 안인지 1차 가드
        if ((char*)next + next_size - DSIZE + WSIZE - 1 > (char*)mem_heap_hi())
            goto do_alloc_copy;

        size_t new_total = curr_size + next_size;
        size_t remain    = new_total - asize;

        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));

        if (remain >= 2*DSIZE) {
            char *rem = (char*)ptr + asize;
            char *rf  = rem + remain - DSIZE;     // FTRP(rem) 직접 계산
            if (rf + WSIZE - 1 > (char*)mem_heap_hi())
                goto do_alloc_copy;               // 2차 가드

            PUT(HDRP(rem), PACK(remain, 0));
            *(unsigned int *)rf = PACK(remain, 0);
        } else {
            PUT(HDRP(ptr), PACK(new_total, 1));
            PUT(FTRP(ptr), PACK(new_total, 1));
        }
        return ptr;
    }

    // B) 뒤가 에필로그면: extend 후 in-place
    if (next_size == 0) {
        size_t need = asize - curr_size;
        size_t extendsz = MAX(need, CHUNKSIZE);
        if (extend_heap(extendsz / WSIZE) != NULL) {
            char *post = (char*)ptr + curr_size;        // = NEXT_BLKP(ptr) 고정
            size_t free_after = GET_SIZE(HDRP(post));
            size_t total  = curr_size + free_after;
            size_t remain = total - asize;

            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));

            if (remain >= 2*DSIZE) {
                char *rem = (char*)ptr + asize;
                char *rf  = rem + remain - DSIZE;
                if (rf + WSIZE - 1 > (char*)mem_heap_hi())
                    goto do_alloc_copy;

                PUT(HDRP(rem), PACK(remain, 0));
                *(unsigned int *)rf = PACK(remain, 0);
            } else {
                PUT(HDRP(ptr), PACK(total, 1));
                PUT(FTRP(ptr), PACK(total, 1));
            }
            return ptr;
        }
    }

    // C) fallback: 새 블록 할당 + 안전 복사
do_alloc_copy: ;
    void *newp = mm_malloc(size);
    if (!newp) return NULL;

    size_t new_total   = GET_SIZE(HDRP(newp));
    size_t old_payload = curr_size - DSIZE;
    size_t new_payload = new_total - DSIZE;

    size_t copy = old_payload;
    if (copy > new_payload) copy = new_payload;
    if (copy > size)        copy = size;

    memcpy(newp, ptr, copy);
    mm_free(ptr);
    return newp;
}

// void *mm_realloc(void *ptr, size_t size)
// {
//     void *oldptr = ptr;    // 기존 블록 포인터
//     void *newptr;          // 새로 할당받을 블록 포인터
//     size_t copySize;       // 복사할 데이터 크기

//     // Step 1: 새 크기만큼 메모리를 할당
//     newptr = mm_malloc(size);
//     if (newptr == NULL)    // 메모리 할당 실패 시 NULL 반환
//         return NULL;

//     // Step 2: 기존 블록의 크기 읽기
//     copySize = GET_SIZE(HDRP(oldptr));

//     // Step 3: 복사할 크기를 요청 크기로 조정 (원래 블록이 더 크면 size까지만 복사)
//     if (size < copySize)
//         copySize = size;

//     // Step 4: 데이터 복사
//     memcpy(newptr, oldptr, copySize);

//     // Step 5: 기존 블록 반환 (free)
//     mm_free(oldptr);

//     return newptr;         // 새 블록 포인터 반환
// }