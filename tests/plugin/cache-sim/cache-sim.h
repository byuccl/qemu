/* 
 * cache.h
 */

#include <stdint.h>


// addresses for ARM 32 bit
typedef uint32_t aarch32_addr_t;

// options for replacement
enum cache_policy_e {
    POLICY_ROUND_ROBIN,
    POLICY_RANDOM
};
typedef enum cache_policy_e cache_policy_t;

// cache options
enum cache_result_e {
    CACHE_RESULT_MISS = 0,
    CACHE_RESULT_HIT,
};
typedef enum cache_result_e cache_result_t;

// create a type for a cache entry
struct cache_entry {
    aarch32_addr_t tag;
    uint8_t valid;
};
typedef struct cache_entry cache_entry_t;

// create a type for address masks
struct cache_mask {
    uint32_t blockOffsetMask;
    uint32_t rowMask;
    uint32_t rowShift;
    uint32_t tagShift;
};
typedef struct cache_mask cache_mask_t;

// this will represent a cache
struct cache_stats {
    cache_entry_t** table;  // pointer to the table containing the addresses
    uint64_t load_hits;     // number of times requested address in cache
    uint64_t load_misses;   // number of times requested address NOT in cache
    uint64_t store_hits;    // same but for writing
    uint64_t store_misses;  // same but for writing
    uint32_t cacheSize;     // total number of bytes of data in the cache
    uint32_t rows;          // number of rows in the cache
    uint32_t associativity; // number of blocks in each row
    uint32_t blockSize;     // size of cache block (bytes)
    union replace_u {
        uint32_t prev;              // previous index (random)
        uint32_t* round_robin;      // array of previous index
    } replace;
    cache_policy_t policy;  // cache replacement policy
    cache_mask_t maskInfo;  // masks for address translation
};
typedef struct cache_stats cache_t;


// macros
#define CREATE_BIT_MASK(x) ((1U << (x)) - 1)
#define LOG_2(x) (31U - (uint32_t) __builtin_clz(x) )


/*
 * Cache size (in bytes) is: (# of rows) * (associativity) * (block size)
 * So to compute # of rows from the others, it is
 *      cache_size / (block_size * associativity)
 */
