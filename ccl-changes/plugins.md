Creating Qemu Plugins
===========================

 - [Official Documentation](https://qemu.readthedocs.io/en/stable/devel/tcg-plugins.html)
 - [Small Discussion](https://stackoverflow.com/questions/58766571/how-to-count-the-number-of-guest-instructions-qemu-executed-from-the-beginning-t)


Building
----------

When you run the `configure` script, make sure to give it the `--enable-plugins` flag.  Then rebuild Qemu by running `make install`.

You will need to register your plugin .so file with the build system.  Edit the file `tests/plugin/Makefile` and add the name of your plugin to the symbol `NAMES`.

Then you will have to run `make plugins` from the main build directory.

Running
--------

When you run Qemu with the plugin, add the following:

```
-plugin [path/to/file.so]
-d plugin
-D plugin_output.log
```

This loads the plugin, enables plugin logging, and redirects it to a file named `plugin_output.log`


Details about writing scripts
=================================

The [Official Documentation](https://qemu.readthedocs.io/en/stable/devel/tcg-plugins.html) has some good examples, but here we will walk through the plugin we have created in this fork.  The plugin source [ldst.c](https://github.com/byuccl/qemu/blob/cache-sim/tests/plugin/ldst.c) will be the reference file for this section.  We recommend you look at this file and the functions as they are mentioned below for maximum understanding.


Installing a plugin
----------------------

If there is an error while running `qemu_plugin_install`, return `!0`.  This is the expected error code.


Registering a callback on block translation
--------------------------------------------

The function `qemu_plugin_install` registers the plugin with Qemu.  It must be named this for each plugin, and have the corresponding macro at the beginning of the function definition.  Note that, when parsing arguments, the first argument does *not* contain the name of the program, as one might expect when running a C program.

This function calls two Qemu plugin functions.  The first, `qemu_plugin_register_vcpu_tb_trans_cb`, registers a function named `put_cbs_in_tbs` to be called whenever a basic block (tb) is translated.  The second, `qemu_plugin_register_atexit_cb`, registers the function `plugin_exit` to be called when Qemu exits, similar to the C library function `atexit`.


Registering a callback on instruction execution
-------------------------------------------------

When our function `put_cbs_in_tbs` is called at each tb translation event, we use it to register even more callbacks for instruction execution.  You can request the number of instructions in a tb with `qemu_plugin_tb_n_insns`, and then iterate over each of those instructions with `qemu_plugin_tb_get_insn`.

There are two kinds of callbacks that can be registered with any given instruction.  The first, `qemu_plugin_register_vcpu_insn_exec_cb`, registers a function to be called when an instruction is executed.  The second, `qemu_plugin_register_vcpu_mem_cb`, registers a function to be called when a memory operation occurs.  We have not been able to find a lot of documentation about this functionality, but we know that this at least works for `load` instructions.  There are also `inline` versions of these functions.  See `qemu-plugin.h` for more information about them, as well as the example plugin `insn.c`.

COMING SOON: what information is available in an instruction callback?



More Resources
=================

There is some discussion of how the TCG implements the plugin functionality in the comments in the file `accel/tcg/plugin-gen.c`
