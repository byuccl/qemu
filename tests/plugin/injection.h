/*
 * injection.h
 * 
 * helps with injecting faults into the cache
 */

#include <stdint.h>

struct injection_plan {
    uint64_t sleepCycles;
    uint64_t cacheRow;
    uint64_t cacheSet;
    uint64_t cacheBit;
};
typedef struct injection_plan injection_plan_t;
