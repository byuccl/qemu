#ifndef __ICACHE_H
#define __ICACHE_H

/*
 * icache.h
 */


/********************************** includes **********************************/
#include "cache-common.h"


/******************************* default values *******************************/
// these defaults are for the cache hierarchy on the ARM Cortex-A9 as found
//  in the Xilinx ZYNQ-7000
#define ICACHE_SIZE_BYTES       (32768)
#define ICACHE_ASSOCIATIVITY    (4)
#define ICACHE_BLOCK_SIZE       (32)
#define ICACHE_POLICY           (POLICY_RANDOM)


/**************************** function prototypes *****************************/
int icache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                cache_policy_t policy);
void free_icache(void);
void icache_cleanup(void);
void icache_stats(void);
void icache_load(uint64_t vaddr);


#endif  /* __ICACHE_H */
