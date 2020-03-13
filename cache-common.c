#include "cache-common.h"


// Returns the log2 of "num" rounded up to the nearest integer.
int log2_roundup(int num)
{
  int power2;
  int exp;

  for (exp = 0, power2 = 1; power2 < num; power2 <<= 1) {
    exp += 1;
  }
  return exp;
}
