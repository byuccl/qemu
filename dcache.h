#ifndef __DCACHE_H
#define __DCACHE_H

#include <inttypes.h>

// default cache configuration
#define DCACHE_SIZE 32768
#define DCACHE_WAYS 4
#define DCACHE_LINE_SIZE 32

// function prototypes
void dcache_init(int size, int ways, int line_size, int replace_policy,
                        int load_miss_penalty, int store_miss_penalty);
void dcache_store(uint32_t addr, uint32_t val);
void dcache_load(uint32_t addr);
void dcache_swp(uint32_t addr);


#endif  /* __DCACHE_H */
