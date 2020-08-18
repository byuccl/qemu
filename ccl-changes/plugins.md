Creating QEMU Plugins
===========================

In this file we discuss some general ideas about writing QEMU plugins.

This guide was written for QEMU 4.2.0, the first version with plugin support.  It is possible that significant changes have occurred since this was written.

Some relevant links:

 - [What is the TCG?](https://wiki.qemu.org/Documentation/TCG) (QEMU Wiki)
 - [Official Plugin Documentation](https://qemu.readthedocs.io/en/stable/devel/tcg-plugins.html) (QEMU Docs)
 - [Small Discussion on Plugins](https://stackoverflow.com/questions/58766571/how-to-count-the-number-of-guest-instructions-qemu-executed-from-the-beginning-t) (Stack Overflow)


Building
----------

When you run the `configure` script, make sure to give it the `--enable-plugins` flag.  Then rebuild Qemu by running `make install`.

You will need to register your plugin .so (shared object) file with the build system.  Edit the file `tests/plugin/Makefile` and add the name of your plugin to the symbol `NAMES`.

Then you will have to run `make plugins` from the main build directory.


Running
--------

When you run Qemu with the plugin, add the following:

```
-plugin [path/to/file.so]
-d plugin
-D plugin_output.log
```

- `-plugin` loads the plugin
- `-d plugin` enables plugin logging
- `-D plugin_output.log` redirects plugin logging to a file named `plugin_output.log`


Details about writing plugins
=================================

The [Official Documentation](https://qemu.readthedocs.io/en/stable/devel/tcg-plugins.html) has some good examples, but here we will walk through the plugin we have created in this fork.  The plugin source [cache.c](https://github.com/byuccl/qemu/blob/cache-sim/tests/plugin/cache.c) will be the reference file for this section.  We recommend you look at this file and the functions as they are mentioned below for maximum understanding.


Installing a plugin
----------------------

If there is an error while running `qemu_plugin_install`, return `!0`.  This is the expected error code.

The function `qemu_plugin_install` registers the plugin with Qemu.  It must be named this for each plugin, and have the corresponding macro at the beginning of the function definition.  Note that, when parsing arguments, the first argument does *not* contain the name of the program, as one might expect when running a C program.

Arguments are passed to the plugin using the following syntax:
```
-plugin libcache.so,arg=0x100000,arg=0x105D30
```
Multiple arguments can be passed to the plugin in this manner.


Registering a callback on block translation
--------------------------------------------

In our example, the function `qemu_plugin_install` calls two Qemu plugin functions.  The first, `qemu_plugin_register_vcpu_tb_trans_cb`, registers a function named `put_cbs_in_tbs` to be called whenever a basic block (tb) is translated.  The second, `qemu_plugin_register_atexit_cb`, registers the function `plugin_exit` to be called when Qemu exits, similar to the C library function `atexit`.


Registering a callback on instruction execution
-------------------------------------------------

When our function `put_cbs_in_tbs` is called at each tb translation event, we use it to register even more callbacks for instruction execution.  You can request the number of instructions in a tb with `qemu_plugin_tb_n_insns`, and then iterate over each of those instructions with `qemu_plugin_tb_get_insn`.

There are two kinds of callbacks that can be registered with any given instruction.  The first, `qemu_plugin_register_vcpu_insn_exec_cb`, registers a function to be called when an instruction is executed.  The second, `qemu_plugin_register_vcpu_mem_cb`, registers a function to be called when a memory operation occurs.  There are also `inline` versions of these functions.  See `qemu-plugin.h` for more information about them, as well as the example plugin `insn.c`.


Callbacks on Instruction Execution
-----------------------------------

The file [qemu-plugin.h](https://github.com/byuccl/qemu/blob/cache-sim/include/qemu/qemu-plugin.h) defines an API of sorts that the plugin can use to get information from the running QEMU instance.  We will examine a few parts of this file.

Normal instruction callbacks are defined with the following prototype:
```c
typedef void (*qemu_plugin_vcpu_udata_cb_t)(unsigned int vcpu_index,
                                            void *userdata);
```

On a instruction execution callback, the function 1) knows which CPU it is being executed on and 2) has some data the application programmer passed in.  This can be whatever you want.  In our file, `cache.c`, in the function `parse_instruction`, we pass in the address of the instruction being executed.

There is also a different type of instruction callback that can be executed on instructions which access memory:
```c
typedef void
(*qemu_plugin_vcpu_mem_cb_t)(unsigned int vcpu_index,
                             qemu_plugin_meminfo_t info, uint64_t vaddr,
                             void *userdata);
```

There are 2 additional arguments to these kinds of callbacks.  The `info` can be used to query more information about the instruction, and `vaddr` could be the address in memory being operated on.

See `hotpages.c`, an example plugin included with QEMU, for another example of how this part works.


More Resources
=================

There is some discussion of how the TCG implements the plugin functionality in the comments in the file `accel/tcg/plugin-gen.c`
