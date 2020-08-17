#ifndef __ICACHE_H
#define __ICACHE_H

/*
 * icache.h
 */


/********************************** includes **********************************/
#include "cache-common.h"
#include "arm-disas.h"


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
arch_word_t icache_get_addr(uint64_t cacheRow, uint64_t cacheSet);
const cache_t* icache_get_ptr(void);
void icache_invalidate_all(void);
int icache_is_cache_inst(insn_op_t* insn_op_data);


#endif  /* __ICACHE_H */
