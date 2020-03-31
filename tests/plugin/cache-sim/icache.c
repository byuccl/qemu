/*
 * icache.c
 */

/********************************** includes **********************************/
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <qemu-plugin.h>

#include "icache.h"
#include "l2cache.h"


/********************************** globals ***********************************/
static cache_t icache;


/********************************* functions **********************************/
// functions are mostly wrappers around common cache functions

/*
 * Initialize all of the Instruction cache structures
 */
int icache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                cache_policy_t policy)
{
    init_cache_struct(&icache, cacheSize, associativity, blockSize, policy);

    atexit(icache_cleanup);

    return 0;
}

/*
 * Clean up cache structures at the end
 */
void free_icache(void) {
    free_cache_struct(&icache);
}

/*
 * To be called at exit, cleans up data structures.
 */
void icache_cleanup(void) {
    free_icache();
}


/*
 * Print out stats about the last run.
 */
void icache_stats(void) {
    g_autoptr(GString) out = g_string_new("");
    
    g_string_printf(out,        "icache load hits:     %10ld\n", icache.load_hits);
    g_string_append_printf(out, "icache load misses:   %10ld\n", icache.load_misses);

    qemu_plugin_outs(out->str);
}


/*
 * Handle all icache accesses.
 */
void icache_load(uint64_t vaddr) {
    cache_result_t result = cache_load_common(&icache, vaddr);

    if (result == CACHE_RESULT_MISS) {
        // call L2 cache
        l2cache_load(vaddr);
    }

    return;
}
