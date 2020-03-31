/*
 * cache-common.c
 * Functions that all of the caches will use.
 * See cache-common.h for function prototypes.
 */

/********************************** includes **********************************/
#include <stdlib.h>
#include <string.h>

#include "cache-common.h"


/********************************* functions **********************************/

/*
 * Initialize the cache structure.
 * Allocate memory for the table.
 */
int init_cache_struct(cache_t* cp, uint32_t cacheSize, 
                        uint32_t associativity, uint32_t blockSize,
                        cache_policy_t policy)
{
    // how many bytes in a row?
    uint32_t rowBytes = blockSize * associativity;
    // compute the number of rows
    uint32_t numRows = cacheSize / rowBytes;
    // set up the struct
    cp->cacheSize     = cacheSize;
    cp->rows          = numRows;
    cp->associativity = associativity;
    cp->blockSize     = blockSize;
    cp->policy        = policy;

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
    if (policy == POLICY_ROUND_ROBIN) {
        cp->replace.round_robin = malloc(sizeof(uint32_t) * numRows);
        memset(cp->replace.round_robin, 0, sizeof(uint32_t) * numRows);
    }

    // init the cache struct counters to 0
    cp->load_hits = 0;
    cp->load_misses = 0;
    cp->store_hits = 0;
    cp->store_misses = 0;

    return 0;
}


/*
 * Release memory allocated for the cache struct.
 */
void free_cache_struct(cache_t* cp)
{
    // free the big memory
    free(cp->table[0]);
    // free the row of pointers
    free(cp->table);
    // perhaps free replacement table
    if (cp->policy == POLICY_ROUND_ROBIN) {
        free(cp->replace.round_robin);
    }
}


/*
 * See if a given address is in the cache.
 * If it's not, update the cache table to include the relevant block.
 */
cache_result_t cache_load_common(cache_t* cp, uint64_t vaddr)
{
    // keep track of next victim
    uint32_t nextRowIdx = 0;

    // convert 64-bit address to be the size of word of the guest architecture
    arch_word_t addr = (arch_word_t) vaddr;

    // uint32_t blockOffsetBits = cp->maskInfo.blockOffsetMask & addr;
    uint32_t rowIdx = (addr >> cp->maskInfo.rowShift) & cp->maskInfo.rowMask;
    uint32_t tagBits = addr >> cp->maskInfo.tagShift;

    // it might be in this row
    cache_entry_t* cacheRow = cp->table[rowIdx];

    // check all the entries in the row
    int i;
    cache_result_t result = CACHE_RESULT_MISS;
    for (i = 0; i < cp->associativity; i+=1) {
        // if valid and tag matches
        if ( (cacheRow[i].valid) && (cacheRow[i].tag == tagBits) ) {
            result = CACHE_RESULT_HIT;
            break;
        }
    }

    if (result) {
        cp->load_hits += 1;
        return CACHE_RESULT_HIT;
    }
    cp->load_misses += 1;

    // otherwise, load the next address
    cache_entry_t* foundSpot = NULL;
    // first look for invalid places to put it
    for (i = 0; i < cp->associativity; i+=1) {
        if (!cacheRow[i].valid) {
            foundSpot = &cacheRow[i];
            break;
        }
    }
    // otherwise, get next victim
    if (foundSpot == NULL) {
        // is it random or round-robin?
        if (cp->policy == POLICY_RANDOM) {
            // get a random spot
            cp->replace.prev = RANDOM_U32(cp->replace.prev);
            nextRowIdx = cp->replace.prev % cp->associativity;
            foundSpot = &cacheRow[nextRowIdx];
        } else {
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
    }

    // NOTE: we can track conflict misses here if we want by checking if the evicted
    //  spot had the valid bit set

    // update the memory spot
    foundSpot->valid = 1;
    foundSpot->tag = tagBits;

    return CACHE_RESULT_MISS;
}


/*
 * See if a given address is in the cache for storing purposes.
 * Currently only counts hits or misses, but does not
 *  update the cache table to include the relevant block if it's missing.
 */
cache_result_t cache_store_common(cache_t* cp, uint64_t vaddr)
{
    // convert 64-bit address to be the size of word of the guest architecture
    arch_word_t addr = (arch_word_t) vaddr;

    // uint32_t blockOffsetBits = cp->maskInfo.blockOffsetMask & addr;
    uint32_t rowIdx = (addr >> cp->maskInfo.rowShift) & cp->maskInfo.rowMask;
    uint32_t tagBits = addr >> cp->maskInfo.tagShift;

    // it might be in this row
    cache_entry_t* cacheRow = cp->table[rowIdx];

    // check all the entries in the row
    int i;
    cache_result_t result = CACHE_RESULT_MISS;
    for (i = 0; i < cp->associativity; i+=1) {
        // if valid and tag matches
        if ( (cacheRow[i].valid) && (cacheRow[i].tag == tagBits) ) {
            result = CACHE_RESULT_HIT;
            break;
        }
    }

    if (result) {
        cp->store_hits += 1;
        return CACHE_RESULT_HIT;
    }
    cp->store_misses += 1;
    return CACHE_RESULT_MISS;

    // Assume no write-allocate for now
}
