#ifndef __CACHE_COMMON_H
#define __CACHE_COMMON_H

/* 
 * cache-common.h
 * 
 * Contains functions which are useful for both the icache and
 *  dcache emulation functions.
 */

#define CACHE_POLICY_ROUND_ROBIN    1
#define CACHE_POLICY_RANDOM         2

int log2_roundup(int num);


#endif  /* __CACHE_COMMON_H */
