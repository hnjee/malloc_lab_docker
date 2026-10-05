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
    "team1",
    /* First member's full name */
    "이현지",
    /* First member's email address */
    "leehnjee@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};


#define ALIGNMENT 8 //정렬 단위 
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) //끝 3비트만 지우는 도장
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4 //헤더/푸터 크기
#define DSIZE 8 //헤더+푸터 크기
#define PADDING 4 
#define CHUNKSIZE (1<<12) //힙을 늘리는 기본 단위 (1<<12 = 2의 12제곱 = 4096바이트)
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#define GET(p) (*(unsigned int *)(p)) //p가 void *라 unsigned int *로 캐스팅하고 역참조
#define PUT(p, val) (*(unsigned int *)(p) = (val)) 
#define PACK(size, alloc) ((size) | (alloc))
#define GET_SIZE(p) (GET(p) & ~0x7) //하위 3개만 0으로 지움 
#define GET_ALLOC(p) (GET(p) & 0x1) //최하위 1개만 가져옴 

//바이트 단위로 포인터 연산하기 위해 block pointer를 char *로 캐스팅 
//즉, 주소는 char *로 바이트 단위로 다루고, 읽고 쓸 때만 GET/PUT이 4바이트로 해석한다
#define HDRP(bp) ((char *)(bp) - WSIZE) //bp-4
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)) //이전 footer 위치: bp-8

/*
 * mm_init - initialize the malloc package.
 */

static char *prologue_bp; 

static void *extend_heap(size_t size){
    //0. 요청 size를 ALIGNMENT 배수로 맞춘다. (0이면 return)
    //1. sbrk로 힙 영역을 늘린다
    //2. 새 영역을 free 블록으로 만든다 -> 주의) 블록 헤더 위치: 이전 에필로그 블럭 위치
    //3. 에필로그 헤더 갱신한다 

    // 블록 크기는 ALIGNMENT 배수로 맞춰야 함 
    if(size == 0) return NULL;
    size = ALIGN(size);

    char *bp = (char *)mem_sbrk(size); //이전 brk를 준다
    if(bp == (void *)-1) return NULL;

    PUT(HDRP(bp), PACK(size, 0)); //블록 헤더   
    PUT(FTRP(bp), PACK(size, 0)); //블록 푸터 
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); //에필로그 헤더 갱신

    return bp;
}

int mm_init(void)
{
    //1. 초기화를 위한 세팅: sbrk로 힙 영역을 받기 -> 4바이트 패딩, prologue 헤더/푸터, epilogue 헤더 세팅
    //2. 힙을 늘린다 -> extend_heap  
    
    char *heap_start = (char *)mem_sbrk(PADDING + WSIZE*3);
    if(heap_start == (void *)-1) return -1; 

    PUT(heap_start, 0); //4바이트 패딩 
    PUT(heap_start + PADDING, PACK(DSIZE, 1)); //prologue 헤더
    PUT(heap_start + PADDING + WSIZE, PACK(DSIZE, 1)); //prologue 푸터
    PUT(heap_start + PADDING + DSIZE, PACK(0, 1)); //epilogue 헤더 

    prologue_bp = heap_start + PADDING + WSIZE; //prologue의 bp로 이동 (푸터 시작지점이 됨)

    if(extend_heap(CHUNKSIZE) == NULL) return -1;

    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{

    //1. size
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
        return NULL;
    else
    {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}