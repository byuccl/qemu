# Disassembling ARM Instructions

Using the QEMU plugin system is great because it abstracts much of the target independent information.  For example, plugins can subscribe to all memory loads, and the address of memory being accessed is immediately available to the plugin.  There is no need to decode the instruction being executed.

However, there is one class of instruction that we still need to take care of decoding, and that is the ones that control the caches.  There are instructions that tell the cache to invalidate a way/line, and on some architectures, the entire cache.

Our interest is in the ARM Cortex-A9, but similar functionality is theoretically possible for ISAs with enough information available.


## Architecture background

The Cortex-A9 has two levels of system caches.  In our implementation, found in the Zynq-7000 SoC, L1 I-cache and D-cache are 32 KB each, and there is a 512 KB L2 unified cache.

See [Zynq TRM](https://www.xilinx.com/support/documentation/user_guides/ug585-Zynq-7000-TRM.pdf), sections 3.2.3 and 3.4.1, for block size, associativity of cache, etc.


## Control Instructions

As an eample of what boot code might look like, consider the code in [boot.S:448](https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L448).

The line
```asm
mcr     p15, 0, r11, c7, c6, 2
```
invalidates a block in a row of the D-cache.

The [Cortex-A9 TRM (table 4.19)](https://developer.arm.com/documentation/ddi0388/f/System-Control/Register-descriptions/CP15-c7-register-summary?lang=en) shows that this line executes the operation [DCISW](https://developer.arm.com/docs/ddi0595/b/aarch32-system-instructions/dcisw).

[Line 224](https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L224)
```asm
mcr	p15, 0, r0, c7, c5, 0
```
shows that the entire I-cache may be invalidated in a single instruction.

(Executes the [ICIALLU instruction](https://developer.arm.com/docs/ddi0595/h/aarch32-system-instructions/iciallu))

<!-- Source: [ARM TRM](http://infocenter.arm.com/help/topic/com.arm.doc.ddi0388f/DDI0388F_cortex_a9_r2p2_trm.pdf) section 4.3.20,  -->


## Decoding Instructions

To understand the encodings for the above instructions, we consulted the [ARM Architectural Reference Manual](https://static.docs.arm.com/ddi0406/c/DDI0406C_C_arm_architecture_reference_manual.pdf).

The code that detects this is found in [arm-disas.c:661](https://github.com/byuccl/qemu/blob/cache-sim/tests/plugin/cache-sim/arm-disas.c#L661).  We recommend studying this file to understand how to decode instructions.  See also [cache.c:157](https://github.com/byuccl/qemu/blob/cache-sim/tests/plugin/cache-sim/cache.c#L157) to the end of the function.


## Accessing registers

An interesting challenge we ran into creating this part of the plugin was that QEMU at this time does/did not support reading system registers.  Presumably it's part of the planned functionality (discussed [here](https://lists.nongnu.org/archive/html/qemu-devel/2020-03/msg08741.html)), but that hasn't been realized yet.

There is a way around this, as discussed in [this Stack Overflow question](https://stackoverflow.com/q/60821772/12940429), but it is definitely a hack.

See the function "[get_cpu_register()](https://github.com/byuccl/qemu/blob/cache-sim/tests/plugin/cache-sim/cache.c#L84)" in our code for more details.


## L2 cache Invalidation

There is a [section in the boot code](https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L325) that invalidates the L2 cache.  However, through testing, we have determined that QEMU does not emulate an L2 cache controller for our target architecture.  Therefore, we do not attempt to catch instructions which perform this operation.

<!-- 
 L2 cache invalidate
 https://developer.arm.com/documentation/ddi0246/f/
 CoreLink Level 2 Cache Controller L2C-310 Technical Reference Manual
 Section 3.1.1 describes the initialization sequence.
 Part of this sequence is to write 0xFFFF to offset 0x77C, so this could be caught
  by the plugin if it knew the base address of the L2 cache controller.
 For an example, see
 https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L348
 As far as I can tell, QEMU does not support having an L2CC. This part of the boot code
  is commented out in my version of the BSP.
  -->
