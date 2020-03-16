#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"

#include "cache-common.h"
#include "icache.h"
#include "cache-trace.h"

// internal representation of the icache
typedef struct Icache {
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
} Icache;
Icache icache;

// function prototypes
void icache_stats(void);
void icache_free(void);
void icache_cleanup(void);


void icache_init(int size, int ways, int line_size, int replace_policy,
                 int load_miss_penalty, int store_miss_penalty)
{
  int ii;

  // Compute the logs of the params, rounded up
  int log_size = log2_roundup(size);
  int log_ways = log2_roundup(ways);
  int log_line_size = log2_roundup(line_size);

  // The number of rows in the table = size / (line_size * ways)
  int log_rows = log_size - log_line_size - log_ways;

  icache.size = 1 << log_size;
  icache.ways = 1 << log_ways;
  icache.line_size = 1 << log_line_size;
  icache.log_line_size = log_line_size;
  icache.rows = 1 << log_rows;
  icache.addr_mask = (1 << log_rows) - 1;

  // Allocate an array of pointers, one for each row
  uint32_t **table = malloc(sizeof(uint32_t *) << log_rows);

  // Allocate the data for the whole cache in one call to malloc()
  int data_size = sizeof(uint32_t) << (log_rows + log_ways);
  uint32_t *data = malloc(data_size);

  // Fill the cache with invalid addresses
  memset(data, ~0, data_size);

  // Assign the pointers into the data array
  int rows = icache.rows;
  for (ii = 0; ii < rows; ++ii) {
    table[ii] = &data[ii << log_ways];
  }
  icache.table = table;
  icache.replace_policy = replace_policy;
  icache.next_way = 0;
  icache.extra_increment_counter = 0;

  icache.replace = NULL;
  if (replace_policy == CACHE_POLICY_ROUND_ROBIN) {
    icache.replace = malloc(sizeof(int) << log_rows);
    memset(icache.replace, 0, sizeof(int) << log_rows);
  }
  icache.load_miss_penalty = load_miss_penalty;
  icache.store_miss_penalty = store_miss_penalty;
  icache.load_hits = 0;
  icache.load_misses = 0;
  icache.store_hits = 0;
  icache.store_misses = 0;

  atexit(icache_cleanup);
}


void icache_stats(void)
{
  uint64_t hits = icache.load_hits + icache.store_hits;
  uint64_t misses = icache.load_misses + icache.store_misses;
  uint64_t total = hits + misses;
  double hit_per = 0;
  double miss_per = 0;
  if (total) {
    hit_per = 100.0 * hits / total;
    miss_per = 100.0 * misses / total;
  }
  printf("\n");
  printf("Icache hits   %10lu %6.2f%%\n", hits, hit_per);
  printf("Icache misses %10lu %6.2f%%\n", misses, miss_per);
  printf("Icache total  %10lu\n", hits + misses);

  // also write out to file
  FILE* cache_file = NULL;
  cache_file = fopen("icache-stats.log", "w");
  fprintf(cache_file, "Icache hits   %10lu %6.2f%%\n", hits, hit_per);
  fprintf(cache_file, "Icache misses %10lu %6.2f%%\n", misses, miss_per);
  fprintf(cache_file, "Icache total  %10lu\n", hits + misses);
  fflush(cache_file);
  fclose(cache_file);
}


void icache_free(void)
{
  free(icache.table[0]);
  free(icache.table);
  free(icache.replace);
  icache.table = NULL;
}


void icache_cleanup(void)
{
  icache_stats();
  icache_free();
}


void icache_load(uint32_t addr) {
  int ii;
  int ways = icache.ways;
  uint32_t cache_addr = addr >> icache.log_line_size;
  int row = cache_addr & icache.addr_mask;
  //printf("icache_load 0x%x\n", addr);
  for (ii = 0; ii < ways; ++ii) {
    if (cache_addr == icache.table[row][ii]) {
      icache.load_hits += 1;
      return;
    }
  }

  icache.load_misses += 1;
  sim_time += icache.load_miss_penalty;

  // Pick a way to replace
  int way;
  if (icache.replace_policy == CACHE_POLICY_ROUND_ROBIN) {
    // Round robin replacement policy
    way = icache.replace[row];
    int next_way = way + 1;
    if (next_way == icache.ways)
      next_way = 0;
    icache.replace[row] = next_way;
  } else {
    // Random replacement policy
    way = icache.next_way;
    icache.next_way += 1;
    if (icache.next_way >= icache.ways)
      icache.next_way = 0;

    // Every 13 replacements, add an extra increment to the next way
    icache.extra_increment_counter += 1;
    if (icache.extra_increment_counter == 13) {
      icache.extra_increment_counter = 0;
      icache.next_way += 1;
      if (icache.next_way >= icache.ways)
        icache.next_way = 0;
    }
  }

  icache.table[row][way] = cache_addr;
}

/*
 * QAPI function to ask for a random address in the cache.
 * The build system generates all of the necessary types.
 */
IcacheAddr* qmp_get_icache_addr(Error **errp) {
  IcacheAddr* info;

  info = (IcacheAddr*)g_malloc0(sizeof(*info));

  int randWay, randRow;

  // get a random row and way
  randRow = rand() % icache.rows;
  randWay = rand() % icache.ways;

  // access the cache table
  info->addr = icache.table[randRow][randWay];
  info->row = randRow;
  info->way = randWay;
  info->valid = (info->addr != (unsigned int)(~0));

  return info;
}
