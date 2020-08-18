#ifndef __CACHE_COMMON_H
#define __CACHE_COMMON_H

/*
 * cache-common.h
 */

/********************************** includes **********************************/
#include "cache-sim.h"
#include "../fault-inject/injection.h"


/******************************** definitions *********************************/

// generates a pseudo-random 32-bit number, based on previous number in sequence
// https://stackoverflow.com/a/52520577/12940429
#define RANDOM_U32(prev) ((uint32_t)(prev * 48271U))

// for dirty bit
#define CACHE_DIRTY     (UINT8_MAX)
#define CACHE_NOT_DIRTY (0)


/**************************** function prototypes *****************************/
int init_cache_struct(cache_t* cp, uint32_t cacheSize, 
                        uint32_t associativity, uint32_t blockSize,
                        cache_policy_t policy);
void free_cache_struct(cache_t* cp);
cache_result_t cache_load_common(cache_t* cp, uint64_t vaddr);
cache_result_t cache_store_common(cache_t* cp, uint64_t vaddr);
arch_word_t cache_get_addr_common(cache_t* cp, uint64_t cacheRow, uint64_t cacheSet);
uint8_t cache_block_valid_common(cache_t* cp, int row, int block);
void cache_invalidate_block_common(cache_t* cp, int row, int block);
int cache_validate_injection_common(cache_t* cp, injection_plan_t* plan);


#endif  /* __CACHE_COMMON_H */
