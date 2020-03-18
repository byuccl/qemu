#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache-common.h"
#include "dcache.h"
#include "cache-trace.h"

// #define DCACHE_PRINT_STATS

typedef struct Dcache {
  int		size;
  int		ways;
  int		line_size;
  int		log_line_size;
  int		rows;
  uint32_t	addr_mask;
  int		replace_policy;
  int		next_way;
  int		extra_increment_counter;
  int		*replace;
  uint32_t	**table;
  int		load_miss_penalty;
  int		store_miss_penalty;
  uint64_t	load_hits;
  uint64_t	load_misses;
  uint64_t	store_hits;
  uint64_t	store_misses;
} Dcache;
Dcache dcache;

// private functions
#ifdef DCACHE_PRINT_STATS
void dcache_stats(void);
#endif
void dcache_free(void);
void dcache_cleanup(void);


void dcache_init(int size, int ways, int line_size, int replace_policy,
                 int load_miss_penalty, int store_miss_penalty)
{
  int ii;

  // Compute the logs of the params, rounded up
  int log_size = log2_roundup(size);
  int log_ways = log2_roundup(ways);
  int log_line_size = log2_roundup(line_size);

  // The number of rows in the table = size / (line_size * ways)
  int log_rows = log_size - log_line_size - log_ways;

  dcache.size = 1 << log_size;
  dcache.ways = 1 << log_ways;
  dcache.line_size = 1 << log_line_size;
  dcache.log_line_size = log_line_size;
  dcache.rows = 1 << log_rows;
  dcache.addr_mask = (1 << log_rows) - 1;

  // Allocate an array of pointers, one for each row
  uint32_t **table = malloc(sizeof(uint32_t *) << log_rows);

  // Allocate the data for the whole cache in one call to malloc()
  int data_size = sizeof(uint32_t) << (log_rows + log_ways);
  uint32_t *data = malloc(data_size);

  // Fill the cache with invalid addresses
  memset(data, ~0, data_size);

  // Assign the pointers into the data array
  int rows = dcache.rows;
  for (ii = 0; ii < rows; ++ii) {
    table[ii] = &data[ii << log_ways];
  }
  dcache.table = table;
  dcache.replace_policy = replace_policy;
  dcache.next_way = 0;
  dcache.extra_increment_counter = 0;

  dcache.replace = NULL;
  if (replace_policy == CACHE_POLICY_ROUND_ROBIN) {
    dcache.replace = malloc(sizeof(int) << log_rows);
    memset(dcache.replace, 0, sizeof(int) << log_rows);
  }
  dcache.load_miss_penalty = load_miss_penalty;
  dcache.store_miss_penalty = store_miss_penalty;
  dcache.load_hits = 0;
  dcache.load_misses = 0;
  dcache.store_hits = 0;
  dcache.store_misses = 0;

  atexit(dcache_cleanup);
}


#ifdef DCACHE_PRINT_STATS
void dcache_stats(void)
{
  uint64_t hits = dcache.load_hits + dcache.store_hits;
  uint64_t misses = dcache.load_misses + dcache.store_misses;
  uint64_t total = hits + misses;
  double hit_per = 0;
  double miss_per = 0;
  if (total) {
    hit_per = 100.0 * hits / total;
    miss_per = 100.0 * misses / total;
  }
  printf("\n");
  printf("Simulation cycle is %ld\n", sim_time);
  printf("\n");
  printf("***Cache simulation***\n");
  printf("Dcache hits   %10lu %6.2f%%\n", hits, hit_per);
  printf("Dcache misses %10lu %6.2f%%\n", misses, miss_per);
  printf("Dcache total  %10lu\n", hits + misses);

  // also write out to file
  FILE* cache_file = NULL;
  cache_file = fopen("dcache-stats.log", "w");
  fprintf(cache_file, "Simulation cycle is %ld\n", sim_time);
  fprintf(cache_file, "\n");
  fprintf(cache_file, "***Cache simulation***\n");
  fprintf(cache_file, "Dcache hits   %10lu %6.2f%%\n", hits, hit_per);
  fprintf(cache_file, "Dcache misses %10lu %6.2f%%\n", misses, miss_per);
  fprintf(cache_file, "Dcache total  %10lu\n", hits + misses);
  fflush(cache_file);
  fclose(cache_file);
}
#endif


void dcache_free(void)
{
  free(dcache.table[0]);
  free(dcache.table);
  free(dcache.replace);
  dcache.table = NULL;
}


void dcache_cleanup(void)
{
#ifdef DCACHE_PRINT_STATS
  dcache_stats();
#endif
  dcache_free();
}


// This function is called by the generated code to simulate
// a dcache load access.
void dcache_load(uint32_t addr)
{
  int ii;
  int ways = dcache.ways;
  uint32_t cache_addr = addr >> dcache.log_line_size;
  int row = cache_addr & dcache.addr_mask;
  //printf("dcache_load 0x%x\n", addr);
  for (ii = 0; ii < ways; ++ii) {
    if (cache_addr == dcache.table[row][ii]) {
      dcache.load_hits += 1;
      return;
    }
  }

  dcache.load_misses += 1;
  sim_time += dcache.load_miss_penalty;

  // Pick a way to replace
  int way;
  if (dcache.replace_policy == CACHE_POLICY_ROUND_ROBIN) {
    // Round robin replacement policy
    way = dcache.replace[row];
    int next_way = way + 1;
    if (next_way == dcache.ways)
      next_way = 0;
    dcache.replace[row] = next_way;
  } else {
    // Random replacement policy
    way = dcache.next_way;
    dcache.next_way += 1;
    if (dcache.next_way >= dcache.ways)
      dcache.next_way = 0;

    // Every 13 replacements, add an extra increment to the next way
    dcache.extra_increment_counter += 1;
    if (dcache.extra_increment_counter == 13) {
      dcache.extra_increment_counter = 0;
      dcache.next_way += 1;
      if (dcache.next_way >= dcache.ways)
        dcache.next_way = 0;
    }
  }

  dcache.table[row][way] = cache_addr;
}


void dcache_store(uint32_t addr, uint32_t val)
{
  int ii;
  int ways = dcache.ways;
  uint32_t cache_addr = addr >> dcache.log_line_size;
  int row = cache_addr & dcache.addr_mask;
  //printf("dcache store 0x%x\n", addr);
  for (ii = 0; ii < ways; ++ii) {
    if (cache_addr == dcache.table[row][ii]) {
      dcache.store_hits += 1;
      return;
    }
  }

  dcache.store_misses += 1;
  sim_time += dcache.store_miss_penalty;

  // Assume no write-allocate for now
}

  
// This function is called by the generated code to simulate
// a dcache load and store (swp) access.
void dcache_swp(uint32_t addr)
{
  dcache_load(addr);
  dcache_store(addr, 0);
}
