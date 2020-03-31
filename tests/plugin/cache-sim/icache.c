/*
 * icache.c
 */

/********************************** includes **********************************/
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <qemu-plugin.h>

#include "icache.h"


/********************************** globals ***********************************/
static cache_t icache;


/******************************** definitions *********************************/

// generates a pseudo-random 32-bit number, based on previous number in sequence
// https://stackoverflow.com/a/52520577/12940429
#define RANDOM_U32(prev) ((uint32_t)(prev * 48271U))


/*
 * Initialize all of the Instruction cache structures
 */
int icache_init(uint32_t cacheSize, uint32_t associativity, uint32_t blockSize,
                cache_policy_t policy)
{
    // how many bytes in a row?
    uint32_t rowBytes = blockSize * associativity;
    // compute the number of rows
    uint32_t numRows = cacheSize / rowBytes;
    // set up the struct
    icache.cacheSize     = cacheSize;
    icache.rows          = numRows;
    icache.associativity = associativity;
    icache.blockSize     = blockSize;
    icache.policy        = policy;

    // set up the bit masks
    uint32_t blockOffsetBits = LOG_2(blockSize);
    uint32_t numRowBits = LOG_2(numRows);
    icache.maskInfo.blockOffsetMask = CREATE_BIT_MASK(blockOffsetBits);
    icache.maskInfo.rowMask = CREATE_BIT_MASK(numRowBits);
    icache.maskInfo.rowShift = blockOffsetBits;
    icache.maskInfo.tagShift = blockOffsetBits + numRowBits;

    // malloc the memory (based on icache.c from Google)
    uint32_t byteSize = sizeof(cache_entry_t) * cacheSize;
    // allocate array of pointers, one for each row
    icache.table = (cache_entry_t**) malloc(sizeof(cache_entry_t*) * numRows);
    // malloc the memory for whole cache all at once - contiguous, yay!
    cache_entry_t* data = malloc(byteSize);
    // fill with invalid data
    memset(data, ~0, byteSize);    
    // assign the pointers to point to the data array
    int i, rowOffset;
    for (i = 0, rowOffset = 0; i < numRows; i++, rowOffset += rowBytes) {
        icache.table[i] = &data[rowOffset];
    }
    // round-robin tracking array
    if (policy == POLICY_ROUND_ROBIN) {
        icache.replace.round_robin = malloc(sizeof(uint32_t) * numRows);
        memset(icache.replace.round_robin, 0, sizeof(uint32_t) * numRows);
    }

    // init the cache struct counters to 0
    icache.hits = 0;
    icache.misses = 0;

    atexit(icache_cleanup);

    return 0;
}

void free_icache(void) {
    // free the big memory
    free(icache.table[0]);
    // free the row of pointers
    free(icache.table);
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
    
    g_string_printf(out, "icache hits: %10ld\n", icache.hits);
    g_string_append_printf(out, "icache misses: %10ld\n", icache.misses);

    qemu_plugin_outs(out->str);
}


/*
 * Handle all icache accesses.
 */
void icache_load(uint64_t vaddr) {
    // keep track of next victim
    uint32_t nextRowIdx = 0;

    // convert 64-bit address
    aarch32_addr_t addr = (aarch32_addr_t) vaddr;

    // uint32_t blockOffsetBits = icache.maskInfo.blockOffsetMask & addr;
    uint32_t rowIdx = (addr >> icache.maskInfo.rowShift) & icache.maskInfo.rowMask;
    uint32_t tagBits = addr >> icache.maskInfo.tagShift;

    // it might be in this row
    cache_entry_t* cacheRow = icache.table[rowIdx];

    // check all the entries in the row
    int i;
    cache_result_t result = CACHE_RESULT_MISS;
    for (i = 0; i < icache.associativity; i+=1) {
        // if valid and tag matches
        if ( (cacheRow[i].valid) && (cacheRow[i].tag == tagBits) ) {
            result = CACHE_RESULT_HIT;
            break;
        }
    }

    if (result) {
        icache.hits += 1;
        return;
    }
    icache.misses += 1;

    // otherwise, load the next address
    cache_entry_t* foundSpot = NULL;
    // TODO: do something better than this
    // first look for invalid places to put it
    for (i = 0; i < icache.associativity; i+=1) {
        if (!cacheRow[i].valid) {
            foundSpot = &cacheRow[i];
            break;
        }
    }
    // otherwise, get next victim
    if (foundSpot == NULL) {
        // is it random or round-robin?
        if (icache.policy == POLICY_RANDOM) {
            // get a random spot
            icache.replace.prev = RANDOM_U32(icache.replace.prev);
            nextRowIdx = icache.replace.prev % icache.associativity;
            foundSpot = &cacheRow[nextRowIdx];
        } else {
            // we remember using the struct
            nextRowIdx = icache.replace.round_robin[rowIdx];
            foundSpot = &cacheRow[nextRowIdx];
            // update next spot
            nextRowIdx++;
            if (nextRowIdx >= icache.associativity) {
                nextRowIdx = 0;
            }
            icache.replace.round_robin[rowIdx] = nextRowIdx;
        }
    }

    // NOTE: we can track conflict misses here if we want by checking if the evicted
    //  spot had the valid bit set

    // update the memory spot
    foundSpot->valid = 1;
    foundSpot->tag = tagBits;

    // TODO: call L2 cache

    return;
}
