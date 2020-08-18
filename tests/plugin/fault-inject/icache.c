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


/*
 * Reset the entirety of the icache.
 */
void icache_invalidate_all(void)
{
    int row, way;
    for (row = icache.rows-1; row >= 0; row--)
    {
        for (way = icache.associativity-1; way >= 0; way--)
        {
            cache_invalidate_block_common(&icache, row, way);
        }
    }
}


/* check for specific opcode setup
 * MCR<c> <coproc>, <opc1>, <Rt>, <CRn>, <CRm>{, <opc2>}
 * mcr	   p15,      0,      r0,   c7,    c5,     0
 * Executes the "ICIALLU" pseudo-instruction
 * https://developer.arm.com/docs/ddi0595/h/aarch32-system-instructions/iciallu
 */
int icache_is_cache_inst(insn_op_t* insn_op_data)
{
    if (
        (insn_op_data->bitfield.coproc == 0xE) &&
        (insn_op_data->bitfield.type == 0x0) &&
        (insn_op_data->bitfield.Rn == 0x7) &&
        (insn_op_data->bitfield.Rm == 0x5) &&
        (insn_op_data->bitfield.Rt2 == 0x0)
    ) {
        return true;
    }
    else
    {
        return false;
    }
}


/*
 * Request the address stored in a given row and column of the cache.
 */
arch_word_t icache_get_addr(uint64_t cacheRow, uint64_t cacheSet)
{
    return cache_get_addr_common(&icache, cacheRow, cacheSet);
}


uint8_t icache_block_valid(int row, int block)
{
    return cache_block_valid_common(&icache, row, block);
}


int icache_validate_injection(injection_plan_t* plan)
{
    return cache_validate_injection_common(&icache, plan);
}
