/* 
 * cache.h
 */

#include <stdint.h>


// turn this on to do specific debugging a certain cache
// #define ENABLE_DEBUG_CACHE_STRUCTS


// this type defines the size of address word on the target platform
// change it to match your needs
typedef uint32_t arch_word_t;

// options for replacement
enum cache_policy_e {
    POLICY_ROUND_ROBIN,
    POLICY_RANDOM
};
typedef enum cache_policy_e replace_policy_t;

// options for write-allocate
enum cache_allocate_e {
    POLICY_WRITE_ALLOCATE,
    POLICY_NO_WRITE_ALLOCATE
};
typedef enum cache_allocate_e allocate_policy_t;

// cache options
enum cache_result_e {
    CACHE_RESULT_MISS = 0,
    CACHE_RESULT_HIT,
};
typedef enum cache_result_e cache_result_t;

// create a type for a cache entry
struct cache_entry {
    arch_word_t tag;
    uint8_t dirty;
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

// subtype for miss tracking
struct cache_miss_types {
    uint64_t compulsory;
    // NOTE: see the below comment about cache miss types
    // uint64_t capacity;
    // uint64_t conflict;
    uint64_t evictions;
};
typedef struct cache_miss_types cache_miss_types_t;

// this will represent a cache
struct cache_stats {
    cache_entry_t** table;  // pointer to the table containing the addresses
    uint64_t load_hits;     // number of times requested address in cache
    uint64_t load_misses;   // number of times requested address NOT in cache
    uint64_t store_hits;    // same but for writing
    uint64_t store_misses;  // same but for writing
    cache_miss_types_t miss_type_counts;    // track counts
    uint32_t cacheSize;     // total number of bytes of data in the cache
    uint32_t rows;          // number of rows in the cache
    uint32_t associativity; // number of blocks in each row
    uint32_t blockSize;     // size of cache block (bytes)
    uint32_t validFlag;     // is data structure valid
    union replace_u {
        uint32_t prev;              // previous index (random)
        uint32_t* round_robin;      // array of previous index
    } replace;
    replace_policy_t replace_policy;    // cache replacement policy
    allocate_policy_t alloc_policy;     // cache write-allocate policy
    cache_mask_t maskInfo;  // masks for address translation
    #ifdef ENABLE_DEBUG_CACHE_STRUCTS
    int debugFlag;          // for debug printing
    #endif
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

/*
 * Notes on cache miss types:
 * From "Computer Architecture: A Quantitative Approach" (6E) pg 81-82.
 * Compulsory miss: "The very first access to a block _cannot_ be in the cache, so the block must be brought into the cache. Compulsory misses are those that occur even if you were to have an infinite-sized cache."
 * Capacity miss: "If the cache cannot contain all the blocks needed during execution of a program, capacity misses (in addition to compulsory misses) will occuer because of blocks being discarded and later retrieved."
 * Conflict miss: "If the block replacement strategy is not fully associative, conflict misses (in addition to compulsory and capacity misses) will occur because a block may be discarded and later retrieved if multiple blocks map to its set and accesses to the different blocks are intermingled."
 *
 * Without doing a lot of extra information tracking, it will be difficult to differentiate between conflict and capacity misses.  For now we will simply report the number of evictions when allocating a new block.
 *
 * See also the discussion of cache trends in Appendix B, section 3 of the same book.
 */
