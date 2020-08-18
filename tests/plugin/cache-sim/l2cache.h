#ifndef __L2CACHE_H
#define __L2CACHE_H

/*
 * l2cache.h
 */


/********************************** includes **********************************/
#include "cache-common.h"


/******************************* default values *******************************/
// these defaults are for the cache hierarchy on the ARM Cortex-A9 as found
//  in the Xilinx ZYNQ-7000
#define L2CACHE_SIZE_BYTES       (524288)
#define L2CACHE_ASSOCIATIVITY    (8)
#define L2CACHE_BLOCK_SIZE       (32)
#define L2CACHE_POLICY           (POLICY_ROUND_ROBIN)


/**************************** function prototypes *****************************/
int l2cache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                cache_policy_t policy);
void free_l2cache(void);
void l2cache_cleanup(void);
void l2cache_stats(void);
void l2cache_load(uint64_t vaddr);
void l2cache_store(uint64_t vaddr);
arch_word_t l2cache_get_addr(uint64_t cacheRow, uint64_t cacheSet);
uint8_t l2cache_block_valid(int row, int block);
int l2cache_validate_injection(injection_plan_t* plan);


#endif  /* __L2CACHE_H */
