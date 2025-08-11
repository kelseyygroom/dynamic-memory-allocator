#include "helpers.h"
#include "debug.h"
#include "icsmm.h"

/* Helper function definitions go here */
int PAGES = 0;


void initialize_heap() {
    void* heap_start = ics_inc_brk();
    PAGES += 1;
    ics_footer* prologue = (ics_footer*) heap_start; // need to assign block size, req. size n allocated bits
    prologue->requested_size = 0;
    prologue->block_size = 0;
    
    void* end = heap_start + 4096 - 8; // that's where we write the epilogue
    ics_header* epilogue = (ics_header*) end;
    epilogue->block_size = 0;
    // *** what is fid && hid supposed to be 4 epilogue n prologue??? ***
    
    // okayyyyy so now the header for the first free block is gonna be heap_start + 8
    // and then the footer for the first free block is gonna be epilogue - 8
    // requested_size is the size of the malloc call
    // block_size is the total number of bytes 4 the block (header + footer + padding)
    // -> lsb of block_size is 0 or 1 for allocated bit
    
    void* h = heap_start + 8;
    ics_free_header* header = (ics_free_header*) h;
    header->header.block_size = 4096-16;     // page size - pro+ep (allocated bit is 0)
    header->header.hid = HEADER_MAGIC;

    end = end - 8;    // where footer is gonna be
    ics_footer* footer = (ics_footer*) end;
    footer->block_size = 4096-16;
    footer->requested_size = 0;     // because it's not a malloc call???
    footer->fid = FOOTER_MAGIC;

    // then create a free block out of the whole space check
    
    // and add to largest bucket of seglist

    seg_buckets[4].freelist_head = header;    // assigning the freelist head for largest bucket
    header->prev = NULL;
    header->next = NULL;
}


int calc_payload_size(int request_size) {
    if (request_size <= 16) request_size = 16;      // smallest block size is 32 
    else if (request_size % 16 != 0) {
        request_size = ((request_size / 16) + 1) * 16;
    } // else request can stay the same
    return request_size;
}

void increase_heap() {
    // need to use footer aka ics_inc_break() value to coalesce with new page
    // remove free block that will be coalesced from segbuckets list so it can be reinserted
    void* old_brk = ics_inc_brk();
    PAGES += 1;
    void* temp = old_brk - 16;       // this will get u to old footer; right before epilogue
    ics_footer* old_footer = (ics_footer*) temp;
    ics_free_header* header = NULL;
    // now check if footer is allocated or not
    if ((old_footer->block_size & 1) == 0) {    // means block is free n need to coalesce
        // gonna want to find the header of that block so that we can update the size and everything
        temp = old_brk - 8 - old_footer->block_size;
        header = (ics_free_header*) temp;      // found header for free block
        
        // REMOVE FROM SEGLIST TO COALESCE
        remove_from_seglist(header);
        header->header.block_size = old_footer->block_size + 4096;  // update block size
        
    } else {            // block is not free, only need to add new page to the freelist
        header = (ics_free_header*) old_brk;      // otherwise we want the new header to be at start of new page
        header->header.block_size = 4096 - 8;                   // subtracting new epilogue
    }
    header->header.hid = HEADER_MAGIC;
    header->next = NULL;
    header->prev = NULL;
    
    temp = old_brk + 4096 - 16;
    ics_footer* footer = (ics_footer*) temp;        // create footer
    footer->block_size = header->header.block_size;
    footer->requested_size = 0;    
    footer->fid = FOOTER_MAGIC;


    if (seg_buckets[4].freelist_head != NULL) {
        header->next = seg_buckets[4].freelist_head;
        seg_buckets[4].freelist_head->prev = header;
    }
    seg_buckets[4].freelist_head = header;


    // add epilogue
    temp = old_brk + 4096 - 8; // that's where we write the epilogue
    ics_header* epilogue = (ics_header*) temp;
    epilogue->block_size = 0;

}

int find_bucket(int size) {
    int j = 0;
    for (int i = 0; i <= 4; i++) {
        if (seg_buckets[i].freelist_head == NULL) j++;
        else if (seg_buckets[i].max_size >= size) break;     // first potential bucket found
    }
    return j;
}


void* find_free_block(int b, int size) {
    ics_bucket bucket;
    void* free_block = NULL;

    while(free_block == NULL && b <= 4) {   
        bucket = seg_buckets[b];  
        // otherwise if we got to this point we have a potentially valid bucket ??
        // search thru the freelist in that specific bucket and find a free block of the requested size
        ics_free_header* block = bucket.freelist_head;
        while(block != NULL) {
            if (block->header.block_size - 16 >= size) {
                free_block = block;     // this is the block to use
                break;
            } else {
                block = block->next;
            }
        }
        if (block == NULL && free_block == NULL) {     // means uu need to go up to the next size bucket
            b++;
        }
    }
    return free_block;
}


int check_if_head(ics_free_header* header) {
    for (int i = 0; i <=4; i++) {
        if (seg_buckets[i].freelist_head == header) {
            return i;
        }
    }
    return -1;
}


int check_valid_ptr(void* ptr) {
    // PTR is a pointer to header of a block
    ics_free_header* header = (ics_free_header*) ptr;
    void* temp = ptr + header->header.block_size - 8 - 1;
    ics_footer* footer = (ics_footer*) temp;

    // check if header and footer are in managed heap space
    if ((void*) header < (ics_get_brk() - (4096*PAGES))) return -1;
    if ((void*) footer > ics_get_brk()) return -1;

    // check for proper header/footer fields
    if (header->header.hid != 0x100decafbee5) return -1;
    if (footer->fid != 0x0a011dab) return -1;

    // header and footer block sizes are not equal
    if (header->header.block_size != footer->block_size) return -1;

    // check that allocated bit is set for block
    if ((header->header.block_size & 1) != 1) return -1; 
    if ((footer->block_size & 1) != 1) return -1;

    return 0;
}

void remove_from_seglist(ics_free_header* block) {
    if (block->prev != NULL) {
        block->prev->next = block->next;  
    } else {
        int j = check_if_head(block);
        seg_buckets[j].freelist_head = block->next;
    }
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
}

void insert_block_in_seglist(ics_free_header* block) {
    int size = block->header.block_size;
    for (int i = 0; i <= 4; i++) {
        if (seg_buckets[i].max_size >= size) {
            ics_free_header* bucket_head = seg_buckets[i].freelist_head;
            if (bucket_head != NULL) {
                bucket_head->prev = block;
                block->next = bucket_head;   
                block->prev = NULL;
            }
            seg_buckets[i].freelist_head = block;
            break;
        }
    }
}

int case1_free(void* ptr) {
    // CASE 1: both prev and next blocks are allocated. only need to free the current block with no coalescing

    ics_free_header* block = (ics_free_header*) ptr;

    // set allocated bit in header and footer
    int size = block->header.block_size;   
    block->header.block_size = size - 1;            // removing allocated bit

    void * temp = ptr + block->header.block_size - 8;
    ics_footer* footer = (ics_footer*) temp;
    footer->block_size = size - 1;                   // removing allocated bit

    insert_block_in_seglist(block);

    return 0;
}

int case2_free(void* ptr) {
    // CASE 2: next block is free. need to coalesce newly freed block w next

    ics_free_header* block = (ics_free_header*) ptr;    // header of block we want to free
    
    void* temp = ptr + block->header.block_size - 1;
    ics_free_header* next = (ics_free_header*) temp;    // header of free block we want to coalesce with

    remove_from_seglist(next);                          // remove from seglist to update next and prev pointers
    int free_size = next->header.block_size;            // get block size to add to header
    int size = block->header.block_size - 1;

    // update header
    block->header.block_size = size + free_size;
    temp = ptr + size + free_size - 8;
    ics_footer* footer = (ics_footer*) temp;

    footer->block_size = size + free_size;
    block->next = NULL;
    block->prev = NULL;

    insert_block_in_seglist(block);
    return 0;
}


int case3_free(void* ptr) {
    // CASE 3: prev block is free. need to coalesce newly freed block w prev
    ics_free_header* block = (ics_free_header*) ptr;


    // find prev block. by going -8 from footer. and then going - block_size from ptr
    void* temp = ptr - 8;
    ics_footer* f = (ics_footer*) temp;     // prev footer
    temp = ptr - f->block_size;
    ics_free_header* prev = (ics_free_header*) temp;

    // remove free block from seglist
    remove_from_seglist(prev);

    // update header
    int free_size = prev->header.block_size;
    int size = block->header.block_size - 1;
    prev->header.block_size = free_size + size;

    temp = ptr + size - 8;
    ics_footer* footer = (ics_footer*) temp;    // footer 4 entire block
    footer->block_size = free_size + size;
    prev->next = NULL; 
    prev->prev = NULL;
    
    insert_block_in_seglist(prev);
    return 0;
}

int case4_free(void* ptr) {
    ics_free_header* block = (ics_free_header*) ptr;

    void* temp = ptr - 8;
    ics_footer* f = (ics_footer*) temp;     // prev footer
    temp = ptr - f->block_size;
    ics_free_header* prev = (ics_free_header*) temp;

    temp = ptr + block->header.block_size - 1;
    ics_free_header* next = (ics_free_header*) temp;    // header of free block we want to coalesce with

    // remove both from seglist
    remove_from_seglist(prev);
    remove_from_seglist(next);

    // just need to add size of (block - 1) and next block to prev header and post footer
    // then add header to seglist

    int size = block->header.block_size - 1;
    int free_size = prev->header.block_size + next->header.block_size;      // size of adj free blocks

    ics_free_header* header = prev;
    temp = ptr + block->header.block_size - 1 + next->header.block_size - 8;
    ics_footer* footer = (ics_footer*) temp;

    header->header.block_size = size + free_size;
    header->next = NULL;
    header->prev = NULL;

    footer->block_size = size + free_size;
    insert_block_in_seglist(header);
    return 0;

}