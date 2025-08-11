#ifndef HELPERS_H
#define HELPERS_H
#include <stdlib.h>
#include <stdint.h>
#include <icsmm.h>
#include <stdbool.h>

int PAGES;      // so i can keep track of how many pages were already used
void initialize_heap();
int calc_payload_size(int request_size);
void increase_heap();
int find_bucket(int size);
void* find_free_block(int b, int size);
int check_if_head(ics_free_header* header);
int check_valid_ptr(void* ptr);
void remove_from_seglist(ics_free_header* ptr);
void insert_block_in_seglist(ics_free_header* block);
int case1_free(void* ptr);
int case2_free(void* ptr);
int case3_free(void* ptr);
int case4_free(void* ptr);
#endif
