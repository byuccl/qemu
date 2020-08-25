/*
 * cache-common.c
 * Functions that all of the caches will use.
 * See cache-common.h for function prototypes.
 */

/********************************** includes **********************************/
#include <stdlib.h>
#include <string.h>

#include "cache-common.h"

#ifdef ENABLE_DEBUG_CACHE_STRUCTS
// for printing
#include <glib.h>
#include <qemu-plugin.h>
#endif


/********************************* functions **********************************/

/*
 * Initialize the cache structure.
 * Allocate memory for the table.
 */
int init_cache_struct(cache_t* cp, uint32_t cacheSize, 
                        uint32_t associativity, uint32_t blockSize,
                        replace_policy_t replace_policy,
                        allocate_policy_t alloc_policy)
{
    // how many bytes in a row?
    uint32_t rowBytes = blockSize * associativity;
    // compute the number of rows
    uint32_t numRows = cacheSize / rowBytes;
    // set up the struct
    cp->cacheSize       = cacheSize;
    cp->rows            = numRows;
    cp->associativity   = associativity;
    cp->blockSize       = blockSize;
    cp->replace_policy  = replace_policy;
    cp->alloc_policy    = alloc_policy;

    // set up the bit masks
    uint32_t blockOffsetBits = LOG_2(blockSize);
    uint32_t numRowBits = LOG_2(numRows);
    cp->maskInfo.blockOffsetMask = CREATE_BIT_MASK(blockOffsetBits);
    cp->maskInfo.rowMask = CREATE_BIT_MASK(numRowBits);
    cp->maskInfo.rowShift = blockOffsetBits;
    cp->maskInfo.tagShift = blockOffsetBits + numRowBits;

    // malloc the memory (based on icache.c from Google)
    uint32_t byteSize = sizeof(cache_entry_t) * cacheSize;
    // allocate array of pointers, one for each row
    cp->table = (cache_entry_t**) malloc(sizeof(cache_entry_t*) * numRows);
    // malloc the memory for whole cache all at once - contiguous, yay!
    cache_entry_t* data = malloc(byteSize);
    // fill with invalid data
    memset(data, ~0, byteSize);    
    // assign the pointers to point to the data array
    int i, rowOffset;
    for (i = 0, rowOffset = 0; i < numRows; i++, rowOffset += rowBytes) {
        cp->table[i] = &data[rowOffset];
    }
    // round-robin tracking array
    if (replace_policy == POLICY_ROUND_ROBIN) {
        cp->replace.round_robin = malloc(sizeof(uint32_t) * numRows);
        memset(cp->replace.round_robin, 0, sizeof(uint32_t) * numRows);
    }

    // init the cache struct counters to 0
    cp->load_hits = 0;
    cp->load_misses = 0;
    cp->store_hits = 0;
    cp->store_misses = 0;

    // cache struct is now valid
    cp->validFlag = 1;

    // debug
    #ifdef ENABLE_DEBUG_CACHE_STRUCTS
    g_autoptr(GString) out = g_string_new("");
    g_string_printf(out, "num blocks: %d\n", cp->cacheSize / cp->blockSize);
    qemu_plugin_outs(out->str);
    #endif

    return 0;
}


/*
 * Release memory allocated for the cache struct.
 */
void free_cache_struct(cache_t* cp)
{
    // no longer valid
    cp->validFlag = 0;
    // free the big memory
    free(cp->table[0]);
    // free the row of pointers
    free(cp->table);
    // perhaps free replacement table
    if (cp->replace_policy == POLICY_ROUND_ROBIN) {
        free(cp->replace.round_robin);
    }
}


/*
 * Allocate an entry in the cache.
 * Local helper function.
 */
static cache_entry_t* cache_get_next_victim(cache_t* cp,
                                    cache_entry_t* cacheRow,
                                    uint32_t rowIdx)
{
    // keep track of next victim
    uint32_t nextRowIdx = 0;
    // return value
    cache_entry_t* foundSpot = NULL;

    // is it random or round-robin?
    if (cp->replace_policy == POLICY_RANDOM) {
        // get a random spot
        cp->replace.prev = RANDOM_U32(cp->replace.prev);
        nextRowIdx = cp->replace.prev % cp->associativity;
        foundSpot = &cacheRow[nextRowIdx];
    } else {       /* POLICY_ROUND_ROBIN */
        // we remember using the struct
        nextRowIdx = cp->replace.round_robin[rowIdx];
        foundSpot = &cacheRow[nextRowIdx];
        // update next spot
        nextRowIdx++;
        if (nextRowIdx >= cp->associativity) {
            nextRowIdx = 0;
        }
        cp->replace.round_robin[rowIdx] = nextRowIdx;
    }

    return foundSpot;
}


/*
 * Get the next spot in the selected cache row that is empty,
 *  that is, still has the dirty bit set.
 * Can possibly return NULL if none are still dirty.
 * Tracks compulsory misses, which will also give an idea of how much
 *  of the total cache memory is being used by the program.
 */
static cache_entry_t* cache_get_next_empty(cache_t* cp,
                                    cache_entry_t* cacheRow)
{
    int i;
    // NULL means didn't find anything
    cache_entry_t* foundSpot = NULL;

    // look for invalid places to put it
    for (i = 0; i < cp->associativity; i+=1) {
        if (cacheRow[i].dirty == CACHE_DIRTY) {
            foundSpot = &cacheRow[i];
            break;
        }
    }

    return foundSpot;
}


/*
 * Checks to see if any of the blocks the specified cache row
 *  match the given tag bits.
 */
static cache_result_t cache_check_resident(cache_t* cp,
                                    cache_entry_t* cacheRow,
                                    arch_word_t tagBits)
{
    int i;
    cache_result_t result = CACHE_RESULT_MISS;

    for (i = 0; i < cp->associativity; i+=1) {
        // if valid and tag matches
        if ( (cacheRow[i].dirty == CACHE_NOT_DIRTY) && (cacheRow[i].tag == tagBits) ) {
            result = CACHE_RESULT_HIT;
            break;
        }
    }

    return result;
}


/*
 * Update a cache block entry by making sure the valid bit is set
 *  and the tag bits have been changed.
 */
static void cache_update_block(cache_entry_t* block,
                            arch_word_t tagBits)
{
    block->dirty = CACHE_NOT_DIRTY;
    block->tag = tagBits;

    return;
}


/*
 * See if a given address is in the cache.
 * If it's not, update the cache table to include the relevant block.
 */
cache_result_t cache_load_common(cache_t* cp, uint64_t vaddr)
{
    // make sure cache struct is still valid
    if (!cp->validFlag)
        return CACHE_RESULT_MISS;

    // keep track of next victim

    // convert 64-bit address to be the size of word of the guest architecture
    arch_word_t addr = (arch_word_t) vaddr;

    // uint32_t blockOffsetBits = cp->maskInfo.blockOffsetMask & addr;
    uint32_t rowIdx = (addr >> cp->maskInfo.rowShift) & cp->maskInfo.rowMask;
    arch_word_t tagBits = addr >> cp->maskInfo.tagShift;

    // it might be in this row
    cache_entry_t* cacheRow = cp->table[rowIdx];

    // check all the entries in the row
    cache_result_t result = cache_check_resident(cp, cacheRow, tagBits);

    // check result - if found, short circuit
    if (result) {
        cp->load_hits += 1;
        return CACHE_RESULT_HIT;
    }
    cp->load_misses += 1;

    // otherwise, load the next address
    cache_entry_t* foundSpot = NULL;
    // first look for invalid places to put it
    foundSpot = cache_get_next_empty(cp, cacheRow);
    // otherwise, get next victim
    if (foundSpot == NULL) {
        foundSpot = cache_get_next_victim(cp, cacheRow, rowIdx);
    }

    // here we track compulsory misses and evictions so we can try to detect thrashing
    if (foundSpot->dirty == CACHE_DIRTY) {
        cp->miss_type_counts.compulsory += 1;
        #ifdef ENABLE_DEBUG_CACHE_STRUCTS
        if (cp->debugFlag) {
            g_autoptr(GString) out = g_string_new("");
            g_string_printf(out, "miss: 0x%04X, 0x%04X\n", rowIdx, tagBits);
            qemu_plugin_outs(out->str);
        }
        #endif
    } else {
        cp->miss_type_counts.evictions += 1;
    }

    // update the memory spot
    cache_update_block(foundSpot, tagBits);

    return CACHE_RESULT_MISS;
}


/*
 * See if a given address is in the cache for storing purposes.
 * Currently only counts hits or misses, but does not
 *  update the cache table to include the relevant block if it's missing.
 */
cache_result_t cache_store_common(cache_t* cp, uint64_t vaddr)
{
    if (!cp->validFlag)
        return CACHE_RESULT_MISS;
    // convert 64-bit address to be the size of word of the guest architecture
    arch_word_t addr = (arch_word_t) vaddr;

    // uint32_t blockOffsetBits = cp->maskInfo.blockOffsetMask & addr;
    uint32_t rowIdx = (addr >> cp->maskInfo.rowShift) & cp->maskInfo.rowMask;
    uint32_t tagBits = addr >> cp->maskInfo.tagShift;

    // it might be in this row
    cache_entry_t* cacheRow = cp->table[rowIdx];

    // check all the entries in the row
    cache_result_t result = cache_check_resident(cp, cacheRow, tagBits);

    // record result - if found, short circuit
    if (result) {
        cp->store_hits += 1;
        return CACHE_RESULT_HIT;
    }
    cp->store_misses += 1;

    // if write allocate
    if (cp->alloc_policy == POLICY_WRITE_ALLOCATE) {
        // otherwise, load the next address
        cache_entry_t* foundSpot = NULL;
        // first look for invalid places to put it
        foundSpot = cache_get_next_empty(cp, cacheRow);
        // otherwise, get next victim
        if (foundSpot == NULL) {
            foundSpot = cache_get_next_victim(cp, cacheRow, rowIdx);
        }
        // track compulsory misses and evictions to try to detect thrashing
        if (foundSpot->dirty == CACHE_DIRTY) {
            cp->miss_type_counts.compulsory += 1;
        } else {
            cp->miss_type_counts.evictions += 1;
        }
        // update the spot with new tag bits
        cache_update_block(foundSpot, tagBits);
    }

    return CACHE_RESULT_MISS;
}


/*
 * It's the tag bits shifted over the correct amount,
 *  with the bits that indicate the row OR'd with that,
 *  which restores most of the address, besides the bits that
 *  indicate the byte block offset.
 */
arch_word_t cache_get_addr_common(cache_t* cp, uint64_t cacheRow, uint64_t cacheSet)
{
    if (!cp->validFlag)
        return 0;
    return (cp->table[cacheRow][cacheSet].tag << cp->maskInfo.tagShift) |
            (cacheRow << cp->maskInfo.rowShift);
}

void cache_invalidate_block_common(cache_t* cp, int row, int block)
{
    if (!cp->validFlag)
        return;
    cp->table[row][block].dirty = CACHE_DIRTY;
}

/*
 * Is valid bit set in cache block?
 * Return value of the bit
 * Also returns 0 if invalid cache struct
 */
uint8_t cache_block_valid_common(cache_t* cp, int row, int block)
{
    if (!cp->validFlag)
        return 0;
    return !(cp->table[row][block].dirty);
}


/*
 * Make sure that the injection parameters are valid for the given cache
 * Return 0 success, non-zero otherwise
 */
int cache_validate_injection_common(cache_t* cp, injection_plan_t* plan)
{
    if (!cp->validFlag)
        return -2;      // invalid cache struct
    if ( (plan->cacheRow > cp->rows-1) ||
            (plan->cacheSet > cp->associativity-1) ||
            (plan->cacheWord > (cp->blockSize * sizeof(arch_word_t))-1) )
    {
        return -1;      // invalid parameters
    }
    else
    {
        return 0;       // valid parameters
    }
}
