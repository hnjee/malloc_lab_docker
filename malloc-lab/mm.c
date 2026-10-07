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

#define WSIZE 4 //헤더/푸터 크기
#define DSIZE 8 //헤더+푸터 크기
#define PADDING 4 
#define MIN_BLOCK_SIZE ALIGN(DSIZE + 1)
#define SPLITSIZE (96)
#define CHUNKSIZE (1<<12) //힙을 늘리는 기본 단위 (1<<12 = 2의 12제곱 = 4096바이트)
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#define GET(p) (*(unsigned int *)(p)) //p가 void *라 unsigned int *로 캐스팅하고 역참조
#define PUT(p, val) (*(unsigned int *)(p) = (val)) 
#define PACK(size, alloc) ((size) | (alloc))
#define GET_SIZE(p) (GET(p) & ~0x7) //하위 3개만 0으로 지움 
#define GET_ALLOC(p) (GET(p) & 0x1) //최하위 1개만 가져옴 
//참고: 0x7, 0x1은 아무 표시 없는 정수 리터럴이라 int인데 GET(p)는 unsigned int -> int가 unsigned int로 변환된 뒤 연산된다.

//바이트 단위로 포인터 연산하기 위해 block pointer를 char *로 캐스팅 
//즉, 주소는 char *로 바이트 단위로 다루고, 읽고 쓸 때만 GET/PUT이 4바이트로 해석한다
#define HDRP(bp) ((char *)(bp) - WSIZE) //bp-4
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)) //이전 footer 위치: bp-8

static char *prologue_bp;
static char *last_search;

static char *coalesce(char *bp);

//-----HEPLER START----//
/*
    0. 요청 size를 ALIGNMENT 배수로 맞춘다. (0이면 return)
    1. sbrk로 힙 영역을 늘린다
    2. 새 영역을 free 블록으로 만든다 -> 주의) 블록 헤더 위치: 이전 에필로그 블럭 위치
    3. 에필로그 헤더 갱신한다 
*/
static char *extend_heap(size_t size){
    if(size == 0) return NULL;
    size_t extend_size = ALIGN(size) > CHUNKSIZE ? ALIGN(size) : CHUNKSIZE;
    char * last_bp = PREV_BLKP((char *)mem_heap_hi() + 1);  
    if (GET_ALLOC(HDRP(last_bp)) == 0){ //힙 마지막 블럭이 free인 경우  
        extend_size -= GET_SIZE(HDRP(last_bp)); //할당할 크기 줄이기 
    }

    char *bp = (char *)mem_sbrk(extend_size); //이전 brk를 준다
    if(bp == (void *)-1) return NULL;

    PUT(HDRP(bp), PACK(extend_size, 0)); //블록 헤더   
    PUT(FTRP(bp), PACK(extend_size, 0)); //블록 푸터 
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); //에필로그 헤더 갱신
    
    return coalesce(bp); //병합한 위치 bp 반환 
}
static void place(char *bp, size_t newsize){
    size_t block_size = GET_SIZE(HDRP(bp));
    if(block_size >= newsize + MIN_BLOCK_SIZE){ //쪼개는 조건
        PUT(HDRP(bp), PACK(newsize, 1)); //블록1 헤더
        PUT(FTRP(bp), PACK(newsize, 1)); //블록1 푸터

        char *bp2 = NEXT_BLKP(bp);
        PUT(HDRP(bp2), PACK(block_size - newsize, 0)); //블록2 헤더
        PUT(FTRP(bp2), PACK(block_size - newsize, 0)); //블록2 푸터
    }else{
        //쪼갤 수 없는 경우 블록 사이즈 그대로 헤더 푸터 갱신
        PUT(HDRP(bp), PACK(block_size, 1)); //헤더
        PUT(FTRP(bp), PACK(block_size, 1)); //푸터 
    }
    return;
}

static void *place_for_malloc(char *bp, size_t newsize){
    size_t block_size = GET_SIZE(HDRP(bp));
    if(block_size >= newsize + MIN_BLOCK_SIZE){ //쪼개는 조건
        if(newsize <= SPLITSIZE){ //앞으로 분할
            PUT(HDRP(bp), PACK(newsize, 1)); //앞블록 헤더
            PUT(FTRP(bp), PACK(newsize, 1)); //앞블록 푸터

            char *bp2 = NEXT_BLKP(bp);
            PUT(HDRP(bp2), PACK(block_size - newsize, 0)); //뒷블록 헤더
            PUT(FTRP(bp2), PACK(block_size - newsize, 0)); //뒷블록 푸터
        }else{ //뒤로 분할 
            PUT(HDRP(bp), PACK(block_size - newsize, 0)); //앞블록 헤더
            PUT(FTRP(bp), PACK(block_size - newsize, 0)); //앞블록 푸터

            char *bp2 = NEXT_BLKP(bp);
            PUT(HDRP(bp2), PACK(newsize, 1)); //뒷블록 헤더
            PUT(FTRP(bp2), PACK(newsize, 1)); //뒷블록 푸터
            return bp2;
        }
    }else{
        //쪼갤 수 없는 경우 블록 사이즈 그대로 헤더 푸터 갱신
        PUT(HDRP(bp), PACK(block_size, 1)); //헤더
        PUT(FTRP(bp), PACK(block_size, 1)); //푸터 
    }
    return bp;
}

static char *coalesce(char *bp){
    unsigned int prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    unsigned int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    if(prev_alloc && next_alloc) return bp; //병합할 필요 없음 

    char *old_bp = bp;
    char *old_next_bp = NEXT_BLKP(old_bp);

    size_t total_size = GET_SIZE(HDRP(bp));
    if(prev_alloc && !next_alloc){ // 1 && 0
        total_size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    }else if(!prev_alloc && next_alloc){ // 0 && 1
        total_size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
    }else{ //0 && 0
        total_size += GET_SIZE(HDRP(NEXT_BLKP(bp))) + GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
    } 
    PUT(HDRP(bp), PACK(total_size, 0));
    PUT(FTRP(bp), PACK(total_size, 0));
    
    if(last_search == old_bp || last_search == old_next_bp){
        last_search = bp;    
    }
    return bp;
}

/*
    - first fit: 힙의 처음부터 훑고, 맞는 첫번째 블록 선택
    - next fit: 직전 검색이 끝난 곳부터 훑기 시작
    - best fit: 모든 빈 블록을 보고, 맞는 것 중 가장 작은 블록 선택
*/
static char *first_fit(size_t newsize){ //first_fit 
    for(char *bp = NEXT_BLKP(prologue_bp); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if((GET_ALLOC(HDRP(bp)) == 0) && (GET_SIZE(HDRP(bp)) >= newsize)){ 
            //현재 힙 내 할당 공간 있는 경우 위치 return
            return bp;
        }
    }
    return NULL; //못찾은 경우 NULL
}

static char *next_fit(size_t newsize){ //next_fit 
    if (last_search == NULL) last_search = prologue_bp;
    
    for(char *bp = last_search; GET_SIZE(HDRP(bp)) > 0; bp = (NEXT_BLKP(bp))){
        if((GET_ALLOC(HDRP(bp)) == 0) && (GET_SIZE(HDRP(bp)) >= newsize)){
            last_search = bp; 
            return bp;
        }
    }  
    for(char *bp = NEXT_BLKP(prologue_bp); bp < last_search; bp = (NEXT_BLKP(bp))){
        if((GET_ALLOC(HDRP(bp)) == 0) && (GET_SIZE(HDRP(bp)) >= newsize)){ 
            last_search = bp; 
            return bp;
        }
    }
    return NULL; //못찾은 경우 NULL
}


static char *best_fit(size_t newsize){
    char *best = NULL;
    for(char *bp = NEXT_BLKP(prologue_bp); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if((GET_ALLOC(HDRP(bp)) == 0)){ 
            size_t size = GET_SIZE(HDRP(bp));
            if(size == newsize){
                return bp;
            }
            if((size > newsize) && (best == NULL || size < GET_SIZE(HDRP(best)))){
                best = bp;
            }
        }
    }
    return best;
}

static void *relocate_block(void *ptr, size_t size){
    void *newptr = mm_malloc(size);
    if(newptr == NULL) return NULL;
    memcpy(newptr, ptr, GET_SIZE(HDRP(ptr))-DSIZE);
    mm_free(ptr);
    return newptr;
}

//-----HEPLER END----//

//1. 초기화를 위한 세팅: sbrk로 힙 영역을 받기 -> 4바이트 패딩, prologue 헤더/푸터, epilogue 헤더 세팅
//2. 힙을 늘린다 -> extend_heap  
int mm_init(void)
{   
    char *heap_start = (char *)mem_sbrk(PADDING + WSIZE*3);
    if(heap_start == (void *)-1) return -1; 

    PUT(heap_start, 0); //4바이트 패딩
    PUT(heap_start + PADDING, PACK(DSIZE, 1)); //prologue 헤더
    PUT(heap_start + PADDING + WSIZE, PACK(DSIZE, 1)); //prologue 푸터
    PUT(heap_start + PADDING + DSIZE, PACK(0, 1)); //epilogue 헤더 

    prologue_bp = heap_start + PADDING + WSIZE; //prologue의 bp로 이동
    last_search = prologue_bp;

    if(extend_heap(CHUNKSIZE) == NULL) return -1;

    return 0;
}

/*
    1. size에 헤더+푸터 크기 더하고, 8의 배수로 올리기
    2. 현재 힙 안에 해당 사이즈를 할당할 수 있는 공간이 있는지 찾기 
    3. 있으면 그곳에 배치 (해당 공간을 쪼갤 수 있는지 확인 필요) 
        없으면 힙 확장 요청 -> 배치    
*/
void *mm_malloc(size_t size)
{
    //1. newsize
    if(size == 0) return NULL;
    size_t newsize = ALIGN(size + DSIZE);
   
    //2. 할당 공간 찾기 
    void *bp = next_fit(newsize);
    if(bp == NULL){ //현재 힙 내에 공간이 없으면 힙 확장
        bp = extend_heap(newsize); 
        if(bp == NULL) return NULL;
    }

    //3. 할당하고 위치 반환 
    return place_for_malloc(bp, newsize);
}

/*
    1. 블럭의 상태가 allocated가 맞는지 확인
    2. ptr이 payload 시작점이 맞는지 확인 -> 이건 나중에 check 함수? 
    3. 맞으면 현재 블럭 free
    4. 병합  
*/
void mm_free(void *ptr)
{
    if(ptr == NULL) return;
    if ((GET_ALLOC(HDRP(ptr)) == 1)){
        PUT(HDRP(ptr), PACK(GET_SIZE(HDRP(ptr)), 0));
        PUT(FTRP(ptr), PACK(GET_SIZE(HDRP(ptr)), 0));

        coalesce(ptr); //병합
        
    }
}

/*
    1. ptr, size 체크 
    2. 새로 할당할 사이즈가 기존 블럭 크기보다 작거나 같으면 -> 거기에 배치
    3. 뒤의 블럭이 free여서 그 블럭까지 합친게 새블럭 크기와 같거나 크면 -> 합치고 거기에 배치
        -> 크면 -> 아예 새로 malloc   
*/
void *mm_realloc(void *ptr, size_t size)
{
    if(ptr == NULL) return mm_malloc(size);

    if(size == 0) {
        mm_free(ptr);
        return NULL;
    }
    
    size_t oldsize = GET_SIZE(HDRP(ptr));
    size_t newsize = ALIGN(size + DSIZE);
    unsigned int nextalloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    unsigned int nextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    
    if(newsize < oldsize){
        if(nextalloc){ //뒷블럭 allocated이면 배치 
            place(ptr, newsize);
        } else{ //뒷블럭 free면 새로운 크기만큼 할당하고 뒷 블럭과 합치기 
            void *next_ptr = NEXT_BLKP(ptr);
            PUT(HDRP(ptr), PACK(newsize, 1)); //새블록 헤더
            PUT(FTRP(ptr), PACK(newsize, 1)); //새블록 푸터
            PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldsize + nextsize - newsize, 0)); //뒷블록 헤더
            PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldsize + nextsize - newsize, 0)); //뒷블록 푸터
            if(last_search == next_ptr) last_search = NEXT_BLKP(ptr);
        }
    } else if(newsize > oldsize){ 
        if(nextalloc){ 
            if(nextsize > 0){ 
                return relocate_block(ptr, size);
            } else{ //에피소드 블럭일때 
                void *newptr = next_fit(newsize);
                if(newptr != NULL){ //힙 안에 적합한 블럭이 있으면 
                    place(newptr, newsize);
                    memcpy(newptr, ptr, oldsize-DSIZE);
                    mm_free(ptr);
                    return newptr;
                }else{ //없으면 꼬리에서 바로 확장 
                    void *bp = mem_sbrk(newsize - oldsize);
                    if(bp == (void *)-1) return NULL;
                    PUT(HDRP(ptr), PACK(newsize, 1)); //new헤더
                    PUT(FTRP(ptr), PACK(newsize, 1)); //new푸터
                    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1)); //에필로그 
                }
            }            
        }else{ //!nextalloc
            size_t block_size = oldsize + nextsize;
            if(block_size >= newsize){
                void *next_ptr = NEXT_BLKP(ptr);
                if(block_size >= newsize + MIN_BLOCK_SIZE){
                    //쪼개기
                    PUT(HDRP(ptr), PACK(newsize, 1));
                    PUT(FTRP(ptr), PACK(newsize, 1));
                    PUT(HDRP(NEXT_BLKP(ptr)), PACK(block_size-newsize, 0));
                    PUT(FTRP(NEXT_BLKP(ptr)), PACK(block_size-newsize, 0));
                } else{
                    PUT(HDRP(ptr), PACK(block_size, 1));
                    PUT(FTRP(ptr), PACK(block_size, 1));
                }
                if(last_search == next_ptr) last_search = ptr;
            } else{ 
                return relocate_block(ptr, size);
            }
        }
    }
    return ptr;
}