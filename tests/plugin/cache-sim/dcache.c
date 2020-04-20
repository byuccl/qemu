/*
 * dcache.c
 */

/********************************** includes **********************************/
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <qemu-plugin.h>

#include "dcache.h"
#include "l2cache.h"


/********************************** globals ***********************************/
static cache_t dcache;


/********************************* functions **********************************/
// functions are mostly wrappers around common cache functions

/*
 * Initialize all of the Instruction cache structures
 */
int dcache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                cache_policy_t policy)
{
    init_cache_struct(&dcache, cacheSize, associativity, blockSize, policy);

    atexit(dcache_cleanup);

    return 0;
}

/*
 * Clean up cache structures at the end
 */
void free_dcache(void) {
    free_cache_struct(&dcache);
}

/*
 * To be called at exit, cleans up data structures.
 */
void dcache_cleanup(void) {
    free_dcache();
}


/*
 * Print out stats about the last run.
 */
void dcache_stats(void) {
    g_autoptr(GString) out = g_string_new("");
    
    g_string_printf(out,        "dcache load hits:     %10ld\n", dcache.load_hits);
    g_string_append_printf(out, "dcache load misses:   %10ld\n", dcache.load_misses);
    g_string_append_printf(out, "dcache store hits:    %10ld\n", dcache.store_hits);
    g_string_append_printf(out, "dcache store misses:  %10ld\n", dcache.store_misses);

    qemu_plugin_outs(out->str);
}


/*
 * Handle all dcache load accesses.
 */
void dcache_load(uint64_t vaddr) {
    cache_result_t result = cache_load_common(&dcache, vaddr);

    if (result == CACHE_RESULT_MISS) {
        // call L2 cache
        l2cache_load(vaddr);
    }

    return;
}


/*
 * Handle all dcache store accesses.
 */
void dcache_store(uint64_t vaddr)
{
    cache_result_t result = cache_store_common(&dcache, vaddr);

    if (result == CACHE_RESULT_MISS) {
        // call L2 cache
        l2cache_store(vaddr);
    }

    return;
}


arch_word_t dcache_get_addr(uint64_t cacheRow, uint64_t cacheSet)
{
    return cache_get_addr_common(&dcache, cacheRow, cacheSet);
}


uint8_t dcache_block_valid(int row, int block)
{
    return cache_block_valid_common(&dcache, row, block);
}

void dcache_invalidate_block(int row, int block)
{
    cache_invalidate_block_common(&dcache, row, block);
}

int dcache_get_num_rows(void)
{
    return dcache.rows;
}

int dcache_get_assoc(void)
{
    return dcache.associativity;
}


int dcache_validate_injection(injection_plan_t* plan)
{
    return cache_validate_injection_common(&dcache, plan);
}
