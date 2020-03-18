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

Under construction, see the [Official Documentation](https://qemu.readthedocs.io/en/stable/devel/tcg-plugins.html) for now
