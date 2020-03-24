Disassembling ARM Instructions
===========================

For our project, we need to know which instructions are accessing what memory locations.  That means that we need to inspect the instruction encoding to determine

- the type of the instruction (load/store)
- the source and/or destination operands
- the values currently in them (?)

The file `ldst.h` contains functions and macros for getting this information.  It has been implemented based on information found in the [ARM Architectural Reference Manual](https://static.docs.arm.com/ddi0406/c/DDI0406C_C_arm_architecture_reference_manual.pdf) (ARM ARM).  Comments denote the tables from which the encodings come.  Initially, this decoding is implemented naively, though we recognize there are probably many instances where this decoding could be optimized.  We welcome any feedback in this regard.
