# Profiling Plugin

We have created a QEMU plugin that can be used for basic profiling of applications.

## Profiling Background

Many profilers that exist today, such as `gprof`, use statistical sampling methods to measure the execution time and function coverage of applications.  However, this plugin uses an instrumentation approach to get more accurate cycle counts.

## Input Format

The plugin expects two arguments: a path to a file with some input information, and another file path where it will write its results.

The file is expected to be in a format like so:
```
main - 1049780; 1049940
abort - 1050100
subFunction - 1056348; 1056356, 1056376
```

Each line begins with the name of the function.  After a dash, the next number is the address, in decimal format, of the entrance to that function.  If a function exit point has been detected, it will come after a semicolon.  If not, for functions like `abort`, this should be omitted.  The plugin should support multiple return points, separated by commas, but this has not been tested thoroughly.

## Example Usage

As an example of how one might use this plugin, we give a high-level overview of our use-case.

We wanted to profile bare-metal applications for the ARM Cortex-A9 part.  We use the compiler flag `-finstrument-functions-after-inlining` ([reference](https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-finstrument-functions-after-inlining)) to generate calls to profiling functions that are user defined.  Calls to these profiling functions are placed at the beginning and ending of each function in the source code.  We define the profiling functions to not do anything other than return immediately.

We then parse the output of `objdump -d binaryFile.elf` to find all calls to these profiling functions.  A Python script compiles all of this information into a file like that shown above.

## Parsing the output

The code we use to parse the output of the plugin is currently private.  It may become public in the future.

The basic idea is to create a call tree based on the plugin output.  Then you can tell how many cycles were spent 1) in each function by itself, and 2) in each function _and_ its children.

## Architecture Dependency

A part of this plugin's functionality is dependent on using it with an ARM target.  Using this, the plugin can print out the return address at each call point by reading the `lr` register.  However, if you don't use this with an ARM target, the rest of the plugin will still work fine.

## Other uses

Theoretically, this plugin can be used to emit other data at any addresses, with very little additional effort.

In addition, it should be possible to add support for other targets to emit the return address.  See the code for more information.

## FreeRTOS Support

In our research, we wanted to profile applications that run [FreeRTOS](https://freertos.org/).  The plugin has to give some additional help to the output parser by reporting when a context switch occurs.  By default this feature is disabled in the code.
