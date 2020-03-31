#ifndef __CACHE_COMMON_H
#define __CACHE_COMMON_H

/*
 * cache-common.h
 */

/********************************** includes **********************************/
#include "cache-sim.h"


/******************************** definitions *********************************/

// generates a pseudo-random 32-bit number, based on previous number in sequence
// https://stackoverflow.com/a/52520577/12940429
#define RANDOM_U32(prev) ((uint32_t)(prev * 48271U))


/**************************** function prototypes *****************************/
int init_cache_struct(cache_t* cp, uint32_t cacheSize, 
                        uint32_t associativity, uint32_t blockSize,
                        cache_policy_t policy);
void free_cache_struct(cache_t* cp);
cache_result_t cache_load_common(cache_t* cp, uint64_t vaddr);
cache_result_t cache_store_common(cache_t* cp, uint64_t vaddr);


#endif  /* __CACHE_COMMON_H */
