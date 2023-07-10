#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your information in the following struct.
 ********************************************************/
team_t team = {
    /* Your student ID */
    "20191619",
    /* Your full name*/
    "DongSeok Lee",
    /* Your email address */
    "ryan1766@sogang.ac.kr",
};

#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<9)
#define INITCHUNKSIZE (1<<3)    
#define LISTLIMIT 10

#define MAX(x,y) ((x)>(y) ? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y)) 

#define PACK(size,alloc) ((size)|(alloc))

#define GET(p) (*(unsigned int *)(p))

#define PUT(p,val) (*(unsigned int *)(p)=(val))

#define GET_SIZE(p) (GET(p) & ~0X7)
#define GET_ALLOC(p) (GET(p) & 0X1)

#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define SET_PTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))
#define NEXT_PTR(ptr) ((char *)(ptr))
#define PREV_PTR(ptr) ((char *)(ptr) + WSIZE)
#define NEXT(ptr) (*(char **)(ptr))
#define PREV(ptr) (*(char **)(PREV_PTR(ptr)))

char *heap_listp = 0;
void *segregated_free_lists[LISTLIMIT];

static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void insert_node(void *ptr, size_t size);
static void delete_node(void *ptr);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    int list;
    
    for (list = 0; list < LISTLIMIT; list++) {
        segregated_free_lists[list] = NULL;
    }
    
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    
    if (extend_heap(INITCHUNKSIZE) == NULL)
        return -1;  
    return 0;
}

static void *extend_heap(size_t words){
    char *bp;
    size_t size;

    size = (words % 2) ? (words +1) * WSIZE : words * WSIZE;
    if((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));
    insert_node(bp,size);       

    return coalesce(bp);        
}

static void insert_node(void *ptr, size_t size) {
    int idx = 0;   
    void *search_ptr = ptr; 
    void *insert_ptr = NULL; 

    while ((idx < LISTLIMIT - 1) && (size > 1)) {
        size = size / 2;
        idx++;
    }

    search_ptr = segregated_free_lists[idx];    

    while ((search_ptr != NULL) && (size > GET_SIZE(HDRP(search_ptr)))) {
        insert_ptr = search_ptr;        
        search_ptr = NEXT(search_ptr);  
    }

    if (search_ptr != NULL) {
        if (insert_ptr != NULL) {       
            SET_PTR(NEXT_PTR(ptr), search_ptr);     
            SET_PTR(PREV_PTR(search_ptr), ptr);     
            SET_PTR(PREV_PTR(ptr), insert_ptr);     
            SET_PTR(NEXT_PTR(insert_ptr), ptr);     
        } else {                        
            SET_PTR(NEXT_PTR(ptr), search_ptr);     
            SET_PTR(PREV_PTR(search_ptr), ptr);     
            SET_PTR(PREV_PTR(ptr), NULL);           
            segregated_free_lists[idx] = ptr;       
        }
    } else {
        if (insert_ptr != NULL) {       
            SET_PTR(NEXT_PTR(ptr), NULL);           
            SET_PTR(PREV_PTR(ptr), insert_ptr);     
            SET_PTR(NEXT_PTR(insert_ptr), ptr);     
        } else {                        
            SET_PTR(NEXT_PTR(ptr), NULL);           
            SET_PTR(PREV_PTR(ptr), NULL);           
            segregated_free_lists[idx] = ptr;       
        }
    }
    
    return;
}

static void delete_node(void *ptr) {
    int idx = 0;
    size_t size = GET_SIZE(HDRP(ptr));

    while ((idx < LISTLIMIT - 1) && (size > 1)) {
        size = size / 2;
        idx++;
    }
    
    if (NEXT(ptr) != NULL) {
        if (PREV(ptr) != NULL) {        
            SET_PTR(PREV_PTR(NEXT(ptr)), PREV(ptr));    
            SET_PTR(NEXT_PTR(PREV(ptr)), NEXT(ptr));    
        } else {    
            SET_PTR(PREV_PTR(NEXT(ptr)), NULL);         
            segregated_free_lists[idx] = NEXT(ptr);     
        }
    } else {
        if (PREV(ptr) != NULL) {        
            SET_PTR(NEXT_PTR(PREV(ptr)), NULL);         
        } else {                        
            segregated_free_lists[idx] = NULL;          
        }
    }
    
    return;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize; 
    char *bp;

    if (heap_listp == 0){
        mm_init();
    }

    /* Ignore spurious*/    
    if (size == 0)
        return NULL;
    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    
    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        bp = place(bp, asize);
        return bp; 
    }
    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    bp = place(bp, asize);
    return bp;
}

static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if(prev_alloc && next_alloc){                   /* Case 1 */
        return bp;
    }
    else if (prev_alloc && !next_alloc){            /* Case 2 */    
        delete_node(bp);                
        delete_node(NEXT_BLKP(bp));     
        
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp),PACK(size,0));
        PUT(FTRP(bp),PACK(size,0));
    }
    else if (!prev_alloc && next_alloc){            /* Case 3 */    
        delete_node(bp);
        delete_node(PREV_BLKP(bp));
        
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp),PACK(size,0));
        PUT(HDRP(PREV_BLKP(bp)),PACK(size,0));
        bp = PREV_BLKP(bp);
    }
    else{                                           /* Case 4 */               
        delete_node(bp);
        delete_node(PREV_BLKP(bp));
        delete_node(NEXT_BLKP(bp));
        
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)),PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)),PACK(size,0));
        bp = PREV_BLKP(bp);
    }
    
    insert_node(bp,size);       
    return bp;
}

static void *find_fit(size_t asize) {
    char *bp;
    char *best_fit = NULL;
    int idx = 0;
    size_t searchsize = asize;

    while (idx < LISTLIMIT) {
        if ((idx == LISTLIMIT - 1) || ((searchsize <= 1) && (segregated_free_lists[idx] != NULL))) {
            bp = segregated_free_lists[idx];

            while (bp != NULL) {
                if (asize <= GET_SIZE(HDRP(bp))) {
                    if (asize == GET_SIZE(HDRP(bp))) {
                        return bp;
                    }
                    else if (best_fit == NULL || GET_SIZE(HDRP(bp)) < GET_SIZE(HDRP(best_fit))) {
                        best_fit = bp;
                    }
                }
                bp = NEXT(bp);
            }
        }

        searchsize >>= 1;
        idx++;
    }
    return best_fit;
}

static void *place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    size_t rsize = csize - asize;

    delete_node(bp);

    if (rsize > (2*DSIZE)) {
        if (asize >= (1<<6)) {
            PUT(HDRP(bp), PACK(rsize, 0));
            PUT(FTRP(bp), PACK(rsize, 0));
            insert_node(bp, rsize);

            PUT(HDRP(NEXT_BLKP(bp)), PACK(asize, 1));
            PUT(FTRP(NEXT_BLKP(bp)), PACK(asize, 1));

            return NEXT_BLKP(bp); 
        } else {
            PUT(HDRP(bp), PACK(asize, 1));
            PUT(FTRP(bp), PACK(asize, 1));
            PUT(HDRP(NEXT_BLKP(bp)), PACK(rsize, 0));
            PUT(FTRP(NEXT_BLKP(bp)), PACK(rsize, 0));
            insert_node(NEXT_BLKP(bp), rsize);
        }
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }

    return bp;
}


/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    if (bp == 0) return;

    size_t size = GET_SIZE(HDRP(bp));

    if (heap_listp == 0){
        mm_init();
    }

    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    
    insert_node(bp,size);

    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    size_t asize = size; 
    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    size_t cur_size = GET_SIZE(HDRP(ptr));
    size_t remainder, extendsize;

    if (cur_size >= asize) {
        return ptr;
    }
   
    size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    int is_next_free = !GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

    if (is_next_free && (cur_size + next_size) >= asize) {
        remainder = cur_size + next_size - asize;

        if (remainder >= 0) {
            delete_node(NEXT_BLKP(ptr));
            
            PUT(HDRP(ptr), PACK(asize + remainder, 1));
            PUT(FTRP(ptr), PACK(asize + remainder, 1));

            return ptr;
        }

        /* If remainder is still not sufficient, extend the heap */
        extendsize = asize - GET_SIZE(HDRP(ptr));

        if (extend_heap(extendsize) == NULL) {
            return NULL;
        }

        remainder += extendsize;
        delete_node(NEXT_BLKP(ptr));

        PUT(HDRP(ptr), PACK(asize + remainder, 1));
        PUT(FTRP(ptr), PACK(asize + remainder, 1));

        return ptr;
    }

    void *new_ptr = mm_malloc(asize - DSIZE);

    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, MIN(size, asize));
    mm_free(ptr);

    return new_ptr;
}

