# QEMU Cache Simulation

This document describes the functionality of the plugin that we have created to do cache emulation with QEMU.  This will primarily describe the operation of the plugin.  See the [page about plugins](plugins.md) for more generic information about how to make plugins.


# Running the Plugin

Currently, the plugin needs a little bit of help to correctly label the different kinds of loads.  The first 2 arguments are the beginning and end addresses for which the plugin should consider the load to be for the Instruction cache.  For example, running `objdump -h` gives the section header information for an ELF file:
```
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00005d00  00100000  00100000  00010000  2**6
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .init         00000018  00105d00  00105d00  00015d00  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  2 .fini         00000018  00105d18  00105d18  00015d18  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  3 .rodata       000002d8  00105d30  00105d30  00015d30  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
...
```
We can see that the first 3 sections are labeled as `CODE`, but `.rodata`, and those after it (not shown) are `DATA` type section.  We want to tell the plugin that anything in the range `0x100000` through `0x105D30` are for code, and to not treat them as data accesses.  It would be nice in the future to figure out a better way of obtaining this information automatically.

### Command Line

Use the following syntax to add running the cache emulation plugin to the QEMU run command:
```
-plugin libcache.so,arg=0x100000,arg=0x105D30
```
You may have to prepend `libcache.so` with the path to the file.

### Fault Injection

In the future, this plugin will also support fault injection.  Then the user will add more arguments, following the same syntax as above, to tell the plugin when and where to inject a fault.


# Statistics

Once the emulation has finished and QEMU exits, the plugin will print out information about the cache.  It will look something like this:
```
insn count:            115390255
load count:             79339224
store count:             1821165
icache load hits:      115390112
icache load misses:          143
dcache load hits:       78214086
dcache load misses:      1125138
dcache store hits:       1405669
dcache store misses:      415496
l2cache load hits:       1124778
l2cache load misses:         503
l2cache store hits:       413611
l2cache store misses:       1885
```

It would be nice to have some way to introspect the state of the cache while QEMU is running, though I don't think plugins support [QMP](https://wiki.qemu.org/Documentation/QMP).
