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
                replace_policy_t replace_policy,
                allocate_policy_t alloc_policy)
{
    init_cache_struct(&dcache, cacheSize, associativity, blockSize,
                      replace_policy, alloc_policy);

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

    // compute miss rate
    uint64_t load_total = dcache.load_hits + dcache.load_misses;
    double loadMissRate = (double)dcache.load_misses / (double)load_total;
    uint64_t store_total = dcache.store_hits + dcache.store_misses;
    double storeMissRate = (double)dcache.store_misses / (double)store_total;
    
    g_string_printf(out,        "dcache load hits:     %12ld\n", dcache.load_hits);
    g_string_append_printf(out, "dcache load misses:   %12ld\n", dcache.load_misses);
    g_string_append_printf(out, "dcache load miss rate: %11.5f%%\n", loadMissRate*100);
    g_string_append_printf(out, "dcache store hits:    %12ld\n", dcache.store_hits);
    g_string_append_printf(out, "dcache store misses:  %12ld\n", dcache.store_misses);
    g_string_append_printf(out, "dcache store miss rate: %10.5f%%\n", storeMissRate*100);

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


/* check for specific opcode setup
 * MCR<c> <coproc>, <opc1>, <Rt>, <CRn>, <CRm>{, <opc2>}
 * mcr     p15,      0,      r11,  c7,    c6,     2
 * Rt - SetWay, bits [31:4]; Level, bits [3:1]; bit 0 reserved
 * A = log2(ASSOCIATIVITY)          (=2)
 * L = log2(LINELEN)                (=5)
 * S = log2(NSETS)                  (=8)
 * B = (L + S)                      (=13)
 * Way, bits[31:32-A] - the number of the way to operate on
 * Set, bits[B-1:L]   - the number of the set to operate on
 */
int dcache_is_cache_inst(insn_op_t* insn_op_data)
{
    if (
        (insn_op_data->bitfield.coproc == 0xE) &&
        (insn_op_data->bitfield.type == 0x0) &&
        (insn_op_data->bitfield.Rn == 0x7) &&
        (insn_op_data->bitfield.Rm == 0x6) &&
        (insn_op_data->bitfield.Rt2 == 0x2)
    ) {
        return true;
    }
    else
    {
        return false;
    }
}


int dcache_validate_injection(injection_plan_t* plan)
{
    return cache_validate_injection_common(&dcache, plan);
}
