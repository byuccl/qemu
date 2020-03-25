#ifndef __ICACHE_H
#define __ICACHE_H

/*
 * icache.h
 */

#include "cache-sim.h"

int icache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                cache_policy_t policy);
void free_icache(void);
void icache_cleanup(void);
void icache_stats(void);
void icache_load(uint64_t vaddr);


#endif  /* __ICACHE_H */
