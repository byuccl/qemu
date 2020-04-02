#ifndef __DCACHE_H
#define __DCACHE_H

/*
 * dcache.h
 */


/********************************** includes **********************************/
#include "cache-common.h"


/******************************* default values *******************************/
// these defaults are for the cache hierarchy on the ARM Cortex-A9 as found
//  in the Xilinx ZYNQ-7000
#define DCACHE_SIZE_BYTES       (32768)
#define DCACHE_ASSOCIATIVITY    (4)
#define DCACHE_BLOCK_SIZE       (32)
#define DCACHE_POLICY           (POLICY_RANDOM)


/**************************** function prototypes *****************************/
int dcache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                cache_policy_t policy);
void free_dcache(void);
void dcache_cleanup(void);
void dcache_stats(void);
void dcache_load(uint64_t vaddr);
void dcache_store(uint64_t vaddr);
const cache_t* dcache_get_ptr(void);
arch_word_t dcache_get_addr(uint64_t cacheRow, uint64_t cacheSet);
int dcache_get_num_rows(void);
int dcache_get_assoc(void);
void dcache_invalidate_block(int row, int block);


#endif  /* __DCACHE_H */
