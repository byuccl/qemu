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
                cache_policy_t policy)
{
    init_cache_struct(&l2cache, cacheSize, associativity, blockSize, policy);

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
    
    g_string_printf(out,        "l2cache load hits:    %10ld\n", l2cache.load_hits);
    g_string_append_printf(out, "l2cache load misses:  %10ld\n", l2cache.load_misses);
    g_string_append_printf(out, "l2cache store hits:   %10ld\n", l2cache.store_hits);
    g_string_append_printf(out, "l2cache store misses: %10ld\n", l2cache.store_misses);

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
