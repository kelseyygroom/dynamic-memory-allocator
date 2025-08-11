# Dynamic Memory Allocator

## Overview

This project is an implementation of the C malloc() and free() functions (named ics_malloc and ics_free). It simulates dynamic memory allocation in a heap using a segregated free list, coalescing, and heap expansion with page requests.

The allocator uses a simulated ics_inc_brk() to request memory pages (4 KB each), ensuring 16-byte alignment for allocated blocks, and maintains headers and footers to store metadata and detect errors.

## Features:
* Custom heap initialization
  * Sets up prologue and epilogue blocks for heap boundary markers
  * Creates an initial free block covering the available page space.
* Memory allocation
  * Validates allocation size (EINVAL for 0, ENOMEM if request exceeds limit).
  * Searches segregated free lists for the smallest available block.
  * Splits free blocks when possible (avoiding splinters smaller than 32 bytes).
  * Expands heap via ics_inc_brk() when necessary (up to 6 pages).
* Memory freeing
  *  Validates pointers (checks heap bounds, magic numbers, allocated bits, and matching header/footer sizes).
  * Coalesces adjacent free blocks into a single larger free block in the cases of: no adjacent free blocks, next block free, previous block free, and both adjacent blocks free.
* Segregated Free Lists
  * Blocks are stored in 5 size-based buckets for faster allocation lookups.
  * Blocks are inserted/removed with linked list management.
