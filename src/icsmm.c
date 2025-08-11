#include "icsmm.h"
#include "debug.h"
#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Malloc implementation: acquires uninitialized memory from  
 * ics_inc_brk() that is 16-byte aligned, as needed.
 *
 * @param size The number of bytes requested to be allocated.
 *
 * @return If successful, the pointer to a valid region of memory of at least the
 * requested size is returned. Otherwise, NULL is returned and errno is set to 
 * ENOMEM - representing failure to allocate space for the request.
 * 
 * If size is 0, then NULL is returned and errno is set to EINVAL - representing
 * an invalid request.
 */
void *ics_malloc(size_t size) { 
    if (size > 4096*6) {
        errno = ENOMEM;
        return NULL;
    }
    if (size == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (PAGES == 0) initialize_heap();      // first malloc call, need to initalize everything
    
    // check each bucket. find the first free space to use
    int block_found = 0;
    int j = find_bucket(size);      // find the first smallest bucket that works w requested size
    void* free_block = NULL;
    
    while (block_found == 0) {    
        free_block = find_free_block(j, size);        // finds the first free block of at least the minimum size

        if (free_block == NULL) {
            if (PAGES == 6) {       // no more space available
                errno = ENOMEM;
                return NULL;
            }
            increase_heap();
        } else {
            block_found = 1;
        }
    }

    // otherwise: free block is found
    ics_free_header* head = (ics_free_header*) free_block;
    remove_from_seglist(free_block);

    int free_size = head->header.block_size;
    int new_size = calc_payload_size(size) + 16;     // now have payload size for new block + header n footer

    int splinter = -1;      // no splinter
    // CHECK FOR SPLINTERS HERE
    if (free_size - new_size < 32) {        // don't split free block
        new_size = free_size;
        splinter = 1;                       // don't add free block to segbuckets
    }

    head->header.block_size = new_size + 1;    // allocated bit on
    head->header.hid = HEADER_MAGIC;
    head->next = NULL; 
    head->prev = NULL;

    void* temp = free_block + new_size - 8;
    ics_footer* foot = (ics_footer*) temp;
    foot->block_size = new_size + 1;
    foot->requested_size = size;    // number of bytes requested by the malloc call
    foot->fid = FOOTER_MAGIC;

    if (splinter == -1) {       // if no splinter, create new free header and footer and add to segbuckets
        temp = free_block + new_size;
        ics_free_header* free_head = (ics_free_header*) temp;      // this is where the new free block header can go
        free_head->header.block_size = free_size - new_size;
        free_head->header.hid = HEADER_MAGIC;
        free_head->next = NULL; 
        free_head->prev = NULL;

        temp = free_block + free_size - 8;
        ics_footer* free_foot = (ics_footer*) temp;
        free_foot->block_size = free_size - new_size;

        // insert free block into appropriate bucket
        insert_block_in_seglist(free_head);
    }

    return free_block+8;
 }

/*
 * Marks a dynamically allocated block as no longer in use and coalesces with 
 * adjacent free blocks. Adds the block to the appropriate bucket according
 * to the block placement policy.
 *
 * @param ptr Address of dynamically allocated memory returned by the function
 * ics_malloc.
 * 
 * @return 0 upon success, -1 if error and set errno accordingly.
 * 
 * If the address of the memory being freed is not valid, this function sets errno
 * to EINVAL. To determine if a ptr is not valid, (i) the header and footer are in
 * the managed  heap space, (ii) check the hid field of the ptr's header for
 * 0x100decafbee5 (iii) check the fid field of the ptr's footer for 0x0a011dab,
 * (iv) check that the block_size in the ptr's header and footer are equal, and (v) 
 * the allocated bit is set in both ptr's header and footer. 
 */
int ics_free(void *ptr) { 
    if (check_valid_ptr(ptr-8) < 0) {
        errno = EINVAL;
        return -1;
    }
    int rtn_val = -9999;
    ics_free_header* cur_block = (ics_free_header*) (ptr-8);        // ptr - 8 == the header

    // get prev block by looking @ footer!!
    ics_footer* prev_block = (ics_footer*) (ptr-16);
    ics_free_header* next_block = (ics_free_header*) (ptr-8 + cur_block->header.block_size-1);  // -1 bc block is allocated
    int prev_alloc = 0;
    int next_alloc = 0;
    
    if ((prev_block->block_size & 1) == 1 || prev_block->block_size == 0) prev_alloc = 1;                    // prev block is allocated or prologue
    if (((next_block->header.block_size & 1) == 1) || next_block->header.block_size == 0) next_alloc = 1;    // next block is epilogue or allocated
    
    if (prev_alloc == 1 && next_alloc == 1) {           // CASE 1: both adj blocks are allocated. only have to free the current block
        rtn_val = case1_free(ptr-8);
    } else if (prev_alloc == 1 && next_alloc == 0) {    // CASE 2: next block is free. need to coalesce newly freed block w next
        rtn_val = case2_free(ptr-8);
    } else if (prev_alloc == 0 && next_alloc == 1) {    // CASE 3: prev block is free. need to coalesce newly freed block w prev
        rtn_val = case3_free(ptr-8);
    } else {                                            // CASE 4: next and prev blocks are both free. need to coalesce entire section UGH
        rtn_val = case4_free(ptr-8);
    }

    return rtn_val;
}
