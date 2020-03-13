#ifndef __ICACHE_H
#define __ICACHE_H

#include <inttypes.h>

// default cache configuration
#define ICACHE_SIZE 32768
#define ICACHE_WAYS 4
#define ICACHE_LINE_SIZE 32

// function prototypes
void icache_init(int size, int ways, int line_size, int replace_policy,
                        int load_miss_penalty, int store_miss_penalty);
void icache_store(uint32_t addr, uint32_t val);
void icache_load(uint32_t addr);
void icache_swp(uint32_t addr);


#endif  /* __ICACHE_H */
