/*
 * l2cache.c
 */

/********************************** includes **********************************/
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <qemu-plugin.h>

#include "l2cache.h"


/********************************** globals ***********************************/
static cache_t l2cache;


/********************************* functions **********************************/
// functions are mostly wrappers around common cache functions

/*
 * Initialize all of the Instruction cache structures
 */
int l2cache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                replace_policy_t replace_policy,
                allocate_policy_t alloc_policy)
{
    init_cache_struct(&l2cache, cacheSize, associativity, blockSize,
                      replace_policy, alloc_policy);

    atexit(l2cache_cleanup);

    return 0;
}

/*
 * Clean up cache structures at the end
 */
void free_l2cache(void) {
    free_cache_struct(&l2cache);
}

/*
 * To be called at exit, cleans up data structures.
 */
void l2cache_cleanup(void) {
    free_l2cache();
}


/*
 * Print out stats about the last run.
 */
void l2cache_stats(void) {
    g_autoptr(GString) out = g_string_new("");

    // compute miss rate
    uint64_t load_total = l2cache.load_hits + l2cache.load_misses;
    double loadMissRate = (double)l2cache.load_misses / (double)load_total;
    uint64_t store_total = l2cache.store_hits + l2cache.store_misses;
    double storeMissRate = (double)l2cache.store_misses / (double)store_total;
    
    g_string_printf(out,        "l2cache load hits:    %12ld\n", l2cache.load_hits);
    g_string_append_printf(out, "l2cache load misses:  %12ld\n", l2cache.load_misses);
    g_string_append_printf(out, "l2cache load miss rate: %10.5f%%\n", loadMissRate*100);
    g_string_append_printf(out, "l2cache store hits:   %12ld\n", l2cache.store_hits);
    g_string_append_printf(out, "l2cache store misses: %12ld\n", l2cache.store_misses);
    g_string_append_printf(out, "l2cache store miss rate: %9.5f%%\n", storeMissRate*100);

    qemu_plugin_outs(out->str);

    // more stats about compulsory misses and evictions
    g_string_printf(out,        "l2cache compulsory misses:%8ld\n",
                    l2cache.miss_type_counts.compulsory);
    g_string_append_printf(out, "l2cache evictions:    %12ld\n",
                    l2cache.miss_type_counts.evictions);
    qemu_plugin_outs(out->str);
}


/*
 * Handle all l2cache load accesses.
 */
void l2cache_load(uint64_t vaddr) {
    cache_result_t result = cache_load_common(&l2cache, vaddr);

    if (result == CACHE_RESULT_MISS) {
        // TODO: hits main memory
    }

    return;
}


/*
 * Handle all l2cache store accesses.
 */
void l2cache_store(uint64_t vaddr)
{
    cache_result_t result = cache_store_common(&l2cache, vaddr);

    if (result == CACHE_RESULT_MISS) {
        // TODO: hits main memory
    }

    return;
}


arch_word_t l2cache_get_addr(uint64_t cacheRow, uint64_t cacheSet)
{
    return cache_get_addr_common(&l2cache, cacheRow, cacheSet);
}


uint8_t l2cache_block_valid(int row, int block)
{
    return cache_block_valid_common(&l2cache, row, block);
}


int l2cache_validate_injection(injection_plan_t* plan)
{
    return cache_validate_injection_common(&l2cache, plan);
}
