How to create a new HMP command
=================================
This is a simple guide of how I added new commands to the Qemu command interface.  It is based on experience only, and does not claim to be the official way to do things.

Based on [this link](https://github.com/qemu/qemu/blob/master/docs/devel/writing-qmp-commands.txt)

My function requests a random address from the Instruction cache.

---------------------

Creating a new QMP command
---------------------------


There has to be a QAPI version of the function first:
```
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
```

This file must include the additional header files:
```
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
```


The QMP function has to be registered in qapi/misc.json
It's possible this could go in a different file, but this one works well enough
```
##
# @IcacheAddr:
#
# Random valid icache address
#
# @addr: 32 bit integer address
# @row: cache row
# @way: cache way (set)
# @valid: has this entry ever been written since reset?
#
# Since 4.2 (ccl)
##
{ 'struct': 'IcacheAddr',
  'data': {'addr': 'int', 'row': 'int', 'way': 'int', 'valid': 'int'} }

##
# @get-icache-addr:
#
# Ask for a random address in the icache.
#
# Since 4.2 (ccl)
##
{ 'command': 'get-icache-addr', 'returns': 'IcacheAddr' }
```
Don't skip the comment headers, or the compilation won't finish.

Even if you want to just return a basic type, like int, you must still create your own custom struct type for this function (or I suppose reuse a previous one).

-------------------------------

Adding a HMP wrapper for the QMP function
--------------------------------------------


You must add the hmp function prototype to include/monitor.hmp.h
```
void hmp_get_icache_addr(Monitor *mon, const QDict *qdict);
```
and even if you don't use the qdict argument, it must be present to compile properly.


The implementation of the HMP function must go in monitor/hmp-cmds.c
```
void hmp_get_icache_addr(Monitor *mon, const QDict *qdict) {
  IcacheAddr *info;
  Error *err = NULL;

  info = qmp_get_icache_addr(&err);
  if (err) {
      monitor_printf(mon, "Could not query icache\n");
      error_free(err);
      return;
  }

  monitor_printf(mon, "addr: 0x%08lX, row: 0x%lX, way: 0x%lX, valid: %s\n", 
      info->addr, info->row, info->way, (info->valid)? "yes" : "no");

  qapi_free_IcacheAddr(info);
}
```

The command must be registered in the file hmp-commands.hx

```
    {
        .name       = "get-icache-addr",
        .args_type  = "",
        .params     = "",
        .help       = "Get random icache address",
        .cmd        = hmp_get_icache_addr,
    },

STEXI
@item get_icache_addr
Get random icache address
ETEXI
```

I put this right before the command "info" and the end of the table.
