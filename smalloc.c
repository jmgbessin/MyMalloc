#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "smalloc.h"

int PAGE_SIZE = 4096;
void* free_list_head;
void* heap_address;
int heap_size;

/*
 * my_init() is called one time by the application program to to perform any 
 * necessary initializations, such as allocating the initial heap area.
 * size_of_region is the number of bytes that you should request from the OS using
 * mmap().
 * Note that you need to round up this amount so that you request memory in 
 * units of the page size, which is defined as 4096 Bytes in this project.
 */
int my_init(int size_of_region) {
    int heap_padding = size_of_region % PAGE_SIZE;
    if (heap_padding != 0) {
        size_of_region += (PAGE_SIZE - heap_padding);
    }
    heap_size = size_of_region;

    int fd = open("/dev/zero", O_RDWR);
    heap_address = mmap(NULL, size_of_region, PROT_WRITE | PROT_READ, 
        MAP_SHARED, fd, 0);

    if (heap_address == MAP_FAILED) {
        return -1;
    }

    // set size of first free block to size of heap
    *((int*) heap_address) = size_of_region;
    // set allocation status of first free block to 0
    *((int*) ((char*)heap_address + 4)) = 0;
    // set free list pointers
    *((void**) ((char*)heap_address + 8)) = NULL;
    *((void**) ((char*)heap_address + 16)) = NULL;

    free_list_head = heap_address;
    close(fd);
    return 0;
}


/*
 * smalloc() takes as input the size in bytes of the payload to be allocated and 
 * returns a pointer to the start of the payload. The function returns NULL if 
 * there is not enough contiguous free space within the memory allocated 
 * by my_init() to satisfy this request.
 */
void *smalloc(int size_of_payload, Malloc_Status *status) {
    if (size_of_payload < 0) {
        status->success = 0;
        status->payload_offset = -1;
        status->hops = -1;
        return NULL;
    }

    int block_padding = size_of_payload % 8;
    if (block_padding != 0) {
        size_of_payload += (8 - block_padding);
    }

    int free_block_found = 0, split_block = 0, hops = 0, free_block_size,
    leftover;
    void* block_head = free_list_head;

    /* Traverses free list until tail is reached */
    while (block_head != NULL) {
        free_block_size = *((int*) block_head);
        leftover = free_block_size - (size_of_payload + 24);

        /* Free block is valid only if 
        (1) it has enough memory to support the header and payload
        (2) the remaining fragmentation is a multiple of 8 in the case that it 
        cannot hold a header by itself and must become padding for the 
        allocation */
        if (leftover >= 0) {
            if (leftover < 24) {
                if (leftover % 8 == 0) {
                    free_block_found = 1;
                    size_of_payload += leftover;
                    break;
                }
            }
            else {
                split_block = 1;
                free_block_found = 1;
                break;
            }
        }

        // go to next block
        block_head = *((void**) ((char*)block_head + 8));
        hops++;
    }

    if (!free_block_found) {
        status->success = 0;
        status->payload_offset = -1;
        status->hops = -1;
        return NULL;
    }
    status->success = 1;
    status->payload_offset = block_head + 24 - heap_address;
    status->hops = hops;

    void* prev_block_head = *((void**) ((char*)block_head + 16));
    void* next_block_head = *((void**) ((char*)block_head + 8));

    /* Split block if leftover fragmentation is large enough to hold a block 
    header */
    if (split_block) {
        void* new_free_block_head = block_head + 24 + size_of_payload;
        *((int*) new_free_block_head) = leftover;
        *((int*) ((char*)new_free_block_head + 4)) = 0;

        if (prev_block_head == NULL) {
            free_list_head = new_free_block_head;
        }

        /* The next two steps replace, in the free list, the free block that is 
        used for allocation with the new free block, the leftover fragmentation 
        of the allocation */

        // update the pointers in the next and previous blocks
        if (prev_block_head != NULL) {
            *((void**) ((char*)prev_block_head + 8)) = new_free_block_head;
        }
        if (next_block_head != NULL) {
            *((void**) ((char*)next_block_head + 16)) = new_free_block_head;
        }
        
        /* populate pointers to next and previous blocks in new, split free 
        block */
        *((void**) ((char*)new_free_block_head + 8)) = next_block_head;
        *((void**) ((char*)new_free_block_head + 16)) = prev_block_head;
    }
    else {
        if (prev_block_head == NULL) {
            free_list_head = next_block_head;
        }

        // remove free block from free list completely
        if (prev_block_head != NULL) {
            *((void**) ((char*)prev_block_head + 8)) = next_block_head;
        }
        if (next_block_head != NULL) {
            *((void**) ((char*)next_block_head + 16)) = prev_block_head;
        }   
    }

    // populate block header
    *((int*) block_head) = size_of_payload + 24;
    *((int*) ((char*)block_head + 4)) = 1;
    // return pointer to start of payload
    return block_head + 24;

}


/*
 * sfree() frees the target block. "ptr" points to the start of the payload.
 * NOTE: "ptr" points to the start of the payload, rather than the block (header).
 */
void sfree(void *ptr)
{
    if (ptr == NULL || ptr < heap_address || ptr > heap_address + heap_size) {
        return;
    }

    // ASSUME THAT PTR POINTS TO THE START OF A PAYLOAD
    // Think about all the places you need update a block size
    // and make sure you are derefercing int*, not void**

    void* traverse_block_head = free_list_head;
    void* next_block_head;
    void* next_next_block_head;
    void* ptr_block_head;
    void* prev_block_head;
    int hops = 0;
    while (1) {

        // check if we have reached end of free list
        ptr_block_head = ptr - 24;
        if (traverse_block_head == NULL) {
            if (hops == 0) {
                prev_block_head = NULL;
            }
            else {
                prev_block_head = next_block_head;
            }
            next_block_head = NULL;
            next_next_block_head = NULL;
        }
        else {
            next_block_head = traverse_block_head;
            if (next_block_head != NULL) {
                next_next_block_head = *((void**) ((char*)next_block_head + 8));
            }
            else {
                next_next_block_head = NULL;
            }
            if (traverse_block_head != NULL) {
                prev_block_head = *((void**) ((char*)traverse_block_head + 16));
            }
            else {
                prev_block_head = NULL;
            }
        }
        
        if (ptr < traverse_block_head || traverse_block_head == NULL) {

            int left_coalesce = prev_block_head != NULL && 
            *((int*) ((char*)prev_block_head + 4)) == 0 &&
            (prev_block_head + *((int*) prev_block_head) == ptr_block_head);

            int right_coalesce = next_block_head != NULL && 
            *((int*) ((char*)next_block_head + 4)) == 0 &&
            (ptr_block_head + *((int*) ptr_block_head) == next_block_head);

            int full_coalesce = left_coalesce && right_coalesce;

            if (full_coalesce) {
                /* update size of left block to include left block, block to be
                freed, and right block */
                *((int*) prev_block_head) += *((int*) ptr_block_head) + 
                *((int*) next_block_head);

                /* update left block to point to right block's right neighbor,
                bypassing right block and block to be freed */
                *((void**) ((char*)prev_block_head + 8)) = *((void**) 
                ((char*)next_block_head + 8));

                /* conversely, update right block's right neighbor to point back 
                to left block */
                if (next_next_block_head != NULL) {
                    *((void**) ((char*)next_next_block_head + 16)) = 
                    prev_block_head;
                }
                
            }
            else if (right_coalesce) {
                /* update size of block to be freed to include right block */
                *((int*) ptr_block_head) += *((int*) next_block_head);

                /* update block to be freed to point to right block's right 
                neighbor, bypassing right block*/
                *((void**) ((char*)ptr_block_head + 8)) = *((void**) 
                ((char*)next_block_head + 8));

                /* converseley, update right block's right neighbor to point
                back to block to be freed */
                if (next_next_block_head != NULL) {
                    *((void**) ((char*)next_next_block_head + 16)) = 
                    ptr_block_head;
                }
                
                /* update block to be freed to point back to left block */
                *((void**) ((char*)ptr_block_head + 16)) = prev_block_head;

                /* update left block to point to block to be freed */
                if (prev_block_head != NULL) {
                    *((void**) ((char*)prev_block_head + 8)) = ptr_block_head;
                }

                /* update block to be freed allocation status to 0 */
                *((int*) ((char*)ptr_block_head + 4)) = 0;
            }
            else if (left_coalesce) {
                /* update size of left block to include block to be freed*/
                *((int*) prev_block_head) += *((int*) ptr_block_head);

                /* update left block to point to right block, bypassing 
                block to be freed */
                *((void**) ((char*)prev_block_head + 8)) = next_block_head;

                /* conversely, update right block to point back to left block */
                if (next_block_head != NULL) {
                    *((void**) ((char*)next_block_head + 16)) = prev_block_head;
                }
            }
            else {
                /* update left block to point to block to be freed */
                if (prev_block_head != NULL) {
                    *((void**) ((char*)prev_block_head + 8)) = ptr_block_head;
                }

                /* update right block to point back to block to be freed */
                if (next_block_head != NULL) {
                    *((void**) ((char*)next_block_head + 16)) = ptr_block_head;
                }
                
                /* update block to be freed to point forwards and backwards
                to right and left blocks respectively */
                *((void**) ((char*)ptr_block_head + 8)) = next_block_head;
                *((void**) ((char*)ptr_block_head + 16)) = prev_block_head;

                /* update block to be freed allocation status to 0 */
                *((int*) ((char*)ptr_block_head + 4)) = 0;
            }

            // update free list head
            if (traverse_block_head == free_list_head || 
                free_list_head == NULL) {
                free_list_head = ptr_block_head;
            }
            break;

        }

        traverse_block_head = next_next_block_head;
        hops++;
    }

}
