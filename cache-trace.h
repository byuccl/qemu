#ifndef __CACHE_TRACE_H
#define __CACHE_TRACE_H

#include <inttypes.h>

extern uint64_t sim_time;

// function prototypes
extern unsigned int get_insn_ticks(uint32_t insn);

#endif  /* __CACHE_TRACE_H */
