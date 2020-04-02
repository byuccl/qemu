/*
 * injection.h
 * 
 * helps with injecting faults into the cache
 */

#include <stdint.h>

typedef enum cache_name_e {
    ICACHE,
    DCACHE,
    L2CACHE,
} cache_name_t;

struct injection_plan {
    uint64_t sleepCycles;
    uint64_t cacheRow;
    uint64_t cacheSet;
    uint64_t cacheBit;
    cache_name_t cacheName;
};
typedef struct injection_plan injection_plan_t;
