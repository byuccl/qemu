#include <stdint.h>

/*
 * Header file for ldst plugin.
 * TODO: look at floating point instructions. Are there any that are load/store?
 * TODO: how to handle conditional loads/stores?
 * See howvec.c for a list of available opcodes for aarch64
 */

/*
 * Types of ARM 32 instructions:
 * Branch and control
 * Data processing
 * Register load and store
 * Multiple register load and store
 * Status register control
 * 
 * Encoding:
 * (https://static.docs.arm.com/ddi0406/c/DDI0406C_C_arm_architecture_reference_manual.pdf)
 * | 31-28 | 27-25 | 24-5 | 4  | 3-0 |
 * |  cond |  op1  |  ?   | op |  ?  |
 */

/*
 * Condition code field (bits 31-28):
 * if == 0b1111, instruciton must be executed unconditionally (A5-216)
 */

/*
 * Opcodes:
 * From table A5-1 (A5-194)
 * op1 = bits 27-25, op = bit 4
 * 
 *  op1 | op | Instruction classes
 * ----------------------------------------------
 *  00x | -  | Data processing and misc (A5-196)
 *  010 | -  | load/store word and unsigned byte (A5-208)
 *  011 | 0  | load/store word and unsigned byte (A5-208)
 *      | 1  | media instructions (A5-209)
 *  10x | -  | Branch, branch w/ link, block data transfer (A5-214)
 *  11x | -  | Coprocessor instructions, supervisor call (A5-215, A7)
 */

/*
 * Data processing & misc (A5-196):
 * | 31-28 | 27-26 | 25 | 24-20 | 19-8 | 7-4 | 3-0 |
 * |  cond |  0 0  | op |  op1  | ?    | op2 |  ?  |
 * 
 * I'm only including the ones that do load/store operations
 * 
 * | op |  op1   | op2  | Instruction class |
 * ------------------------------------------
 * | 0  | ~0xx1x | 1011 | Extra load/store instructions (A5-203)
 * |    |        | 11x1 |  ""
 * |    |  0xx10 | 11x1 |  ""
 * |    |  0xx1x | 1011 | Extra load/store, unprivileged (A5-204)
 * |    |  0xx11 | 11x1 |  ""
 * | 1  |  10000 |  -   | 16-bit immediate load (A8-484)
 * |    |  10100 |  -   | high halfword 16-bit immediate load (A8-491)
 */

/********************************* datatypes **********************************/
// based on what howvec.c did
// typedef enum {
//     INSN_CLASS,
//     INSN_INDIVIDUAL
// } InsnType;

// struct aarch32_insn {
//     const char* name;
//     uint32_t opcode;
//     InsnType type;
// };
// typedef struct aarch32_insn aarch32_insn_t;

// static aarch32_insn_t load_store_insns[] = {
//     /* | 31-28 | 27-25 | 24-20 | 19-16 | 15-8 | 7 | 6-5 | 4 | 3-0 |
//      * |  cond | 0 0 0 |  op1  |  Rn   |  ?   | 1 | op2 | 1 |  ?  | */
//     {"load/store halfword", 0, INSN_CLASS},
// };


/**************************** determine load/store ****************************/
// create a bit mask of the first n lower bits
#ifndef CREATE_BIT_MASK
#define CREATE_BIT_MASK(n) ((1U << (n)) - 1)
#endif
static const uint32_t ARM_OP1_BITS_MASK = CREATE_BIT_MASK(3) << 25;
static const uint32_t ARM_OP_BIT_MASK   = CREATE_BIT_MASK(1) << 4;

// for getting parts of an encoded instruction
#define GET_INSN_OP1_BITS(insn) ((insn & ARM_OP1_BITS_MASK) >> 25)
#define GET_INSN_OP_BIT(insn)   ((insn & ARM_OP_BIT_MASK) >> 4)

// ARM ARM Table A5-1
// TODO: extra instructions also have some
static inline int INSN_IS_LOAD_STORE(uint32_t insn) {
    uint8_t op1_bits = GET_INSN_OP1_BITS(insn);
    uint8_t op_bit   = GET_INSN_OP_BIT(insn);
    return ( (op1_bits == 0x2) ||                   /* 010 , x */
             ((op1_bits == 0x3) && (op_bit == 0))   /* 011 , 0 */
           );
}


/***************************** regular load/store *****************************/
static const uint32_t LDST_OP1_BITS_MASK = CREATE_BIT_MASK(5) << 20;
static const uint32_t LDST_RN_BITS_MASK  = CREATE_BIT_MASK(4) << 16;
static const uint32_t LDST_A_BIT_MASK    = CREATE_BIT_MASK(1) << 25;
static const uint32_t LDST_B_BIT_MASK    = CREATE_BIT_MASK(1) << 4;
static const uint8_t  LDST_UNPRIV_MASK   = 0x12;    // bits 1 and 4 of 5 total
static const uint8_t  LDST_UNPRIV_BITS   = 0x02;    // only bit 1 is set

#define GET_LDST_OP1_BITS(bits) ((bits & LDST_OP1_BITS_MASK) >> 20)
#define GET_LDST_RN_BITS(bits)  ((bits & LDST_RN_BITS_MASK) >> 16)
#define GET_LDST_A_BIT(bits)    ((bits & LDST_A_BIT_MASK) >> 25)
#define GET_LDST_B_BIT(bits)    ((bits & LDST_B_BIT_MASK) >> 4)

struct insn_op {
    uint32_t dataAddr;
    struct bit_field_s {
        uint8_t op1;
        uint8_t Rn;
        uint8_t A;
        uint8_t B;
    } bitfield;
};
typedef struct insn_op insn_op_t;

#define LD_TYPE_BASE 0x100
#define STR_TYPE_BASE 0x001
typedef enum regular_load_store {
    NOT_LOAD_STORE = 0,
    // store instructions
    STR_REG_IMM = STR_TYPE_BASE,
    STR_REG,
    STR_REG_UNPRIV,
    STR_REG_IMM_BYTE,
    STR_REG_BYTE,
    STR_REG_BYTE_UNPRIV,
    // load instructions
    LD_REG_IMM = LD_TYPE_BASE,
    LD_REG_LIT,
    LD_REG,
    LD_REG_UNPRIV,
    LD_REG_IMM_BYTE,
    LD_REG_LIT_BYTE,
    LD_REG_BYTE,
    LD_REG_BYTE_UNPRIV,
} load_store_e;


/*
 * insn_data is a pointer to a struct that will hold data about the instruction
 * insn_bits are the encoded bits of the instruction
 * instructions that come here matched the check in INSN_IS_LOAD_STORE
 * see ARM ARM table A5-15
 */
static load_store_e decode_load_store(insn_op_t* insn_data, uint32_t insn_bits) {
    uint8_t op1 = GET_LDST_OP1_BITS(insn_bits);
    uint8_t Rn  = GET_LDST_RN_BITS(insn_bits);
    uint8_t A   = GET_LDST_A_BIT(insn_bits);
    // uint8_t B   = GET_LDST_B_BIT(insn_bits);
    load_store_e returnVal = NOT_LOAD_STORE;

    // store instructions have in common that the lowest bit of op1 is 0
    // load instructions have in common that the lowest bit of op1 is 1
    // for both, if bit 2 of op1 is 1, then it is a byte operation instead of word
    switch (op1 & 0x1) {
    case 0:         /* store */
        switch (op1 & 0x4) {
        case 0:     /* word */
            // if bit 1 is 1 and bit 4 is 0, then it's unprivileged
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged store word (A8-706)
                returnVal = STR_REG_UNPRIV;
            } else {
                if (A) {
                    // store register word (A8-676)
                    returnVal = STR_REG;
                } else {
                    // store immediate word (A8-674)
                    returnVal = STR_REG_IMM;
                }
            }
            break;
        case 1:     /* byte */
            // if bit 1 is 1 and bit 4 is 0, then it's unprivileged
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged store byte (A8-684)
                returnVal = STR_REG_BYTE_UNPRIV;
            } else {
                if (A) {
                    // store register byte (A8-682)
                    returnVal = STR_REG_BYTE;
                } else {
                    // store immediate byte (A8-680)
                    returnVal = STR_REG_IMM_BYTE;
                }
            }
            break;
        }
        break;

    case 1:         /* load  */
        switch (op1 & 0x4) {
        case 0:     /* word */
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged load register (A8-466)
                returnVal = LD_REG_UNPRIV;
            } else {
                if (A) {
                    // load register word (A8-414)
                    returnVal = LD_REG;
                } else {
                    if (Rn == 0xF) {
                        // load register literal (A8-410)
                        returnVal = LD_REG_LIT;
                    } else {
                        // load register immediate (A8-408)
                        returnVal = LD_REG_IMM;
                    }
                }
            }
            break;
        case 1:     /* byte */
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged load register byte (A8-424)
                returnVal = LD_REG_BYTE_UNPRIV;
            } else {
                if (A) {
                    // load register byte (A8-422)
                    returnVal = LD_REG_BYTE;
                } else {
                    if (Rn == 0xF) {
                        // load register literal byte (A8-420)
                        returnVal = LD_REG_LIT_BYTE;
                    } else {
                        // load register immediate byte (A8-418)
                        returnVal = LD_REG_IMM_BYTE;
                    }
                }
            }
            break;
        }
        break;
    
    default:
        // something really weird just happened, because we AND'd with a single bit
        break;
    }

    // not yet
    // insn_data->bitfield.op1 = op1;
    // insn_data->bitfield.Rn  = Rn;
    // insn_data->bitfield.A   = A;
    // insn_data->bitfield.B   = B;
    return returnVal;
}


/************************* determine extra load/store *************************/
// first, they must match having bits 25-27 all 0
// and op bit == 1   maybe?

static const uint32_t MISC_OP_BIT_MASK   = CREATE_BIT_MASK(1) << 25;
static const uint32_t MISC_OP1_BITS_MASK = CREATE_BIT_MASK(5) << 20;
static const uint32_t MISC_OP2_BITS_MASK = CREATE_BIT_MASK(4) << 4;

#define GET_MISC_OP_BIT(insn)   ((insn & MISC_OP_BIT_MASK) >> 25)
#define GET_MISC_OP1_BITS(insn) ((insn & MISC_OP1_BITS_MASK) >> 20)
#define GET_MISC_OP2_BITS(insn) ((insn & MISC_OP2_BITS_MASK) >> 4)

#define MISC_IS_EXTRA_LDST          (1)     /* A5-203 */
#define MISC_IS_EXTRA_LDST_UNPRIV   (2)     /* A5-204 */
#define MISC_IS_16_BIT_LOAD         (3)     /* A8-484 */
#define MISC_IS_16_BIT_LOAD_HIGH    (4)     /* A8-491 */

/*
 * See table A5-2
 * TODO: this can probably be simplified
 */
static inline int INSN_IS_EXTRA_LOAD_STORE(uint32_t insn) {
    uint8_t op_bit   = GET_MISC_OP_BIT(insn);
    uint8_t op1_bits = GET_MISC_OP1_BITS(insn);
    uint8_t op2_bits = GET_MISC_OP2_BITS(insn);
    int returnVal = 0;

    if (op_bit == 0) {
        uint8_t op1_mask_1 = op1_bits & 0x12;
        uint8_t op2_mask_1 = op2_bits & 0xD;
        uint8_t op1_mask_2 = op1_bits & 0x13;
        // matches the first 2 instances of A5-203 in table A5-2
        if ( (op1_mask_1 != 0x02) &&
            ( (op2_bits == 0xB) || (op2_mask_1 == 0xD) ))
        {
            returnVal = MISC_IS_EXTRA_LDST;
        }
        else if ( (op1_mask_2 == 0x02) &&
                    (op2_mask_1 == 0xD ))
        {
            returnVal = MISC_IS_EXTRA_LDST;
        }
        else if ( (op1_mask_1 == 0x02) &&
                    (op2_bits == 0xB))
        {
            returnVal = MISC_IS_EXTRA_LDST_UNPRIV;
        }
        else if ( (op1_mask_2 == 0x03) &&
                    (op2_mask_1 == 0xD))
        {
            returnVal = MISC_IS_EXTRA_LDST_UNPRIV;
        }
    } else {
        if (op1_bits == 0x10)
        {
            returnVal = MISC_IS_16_BIT_LOAD;
        }
        else if (op1_bits == 0x14)
        {
            returnVal = MISC_IS_16_BIT_LOAD_HIGH;
        }
    }

    return returnVal;
}


/************************** decode extra load/store ***************************/
// op1 and Rn are the same as the regular ld/st instructions
static const uint32_t LDST_EX_OP2_BITS_MASK = CREATE_BIT_MASK(2) << 5;

#define GET_LDST_EX_OP2_BITS(bits) ((bits & LDST_EX_OP2_BITS_MASK) >> 5)

#define STR_EXTRA_TYPE_BASE 0x1001
#define LD_EXTRA_TYPE_BASE 0x1100
typedef enum extra_load_store {
    NOT_EXTRA_LOAD_STORE = 0,
    // store instructions
    STR_REG_IMM_HALF = STR_EXTRA_TYPE_BASE,     /* Table A5-10 */
    STR_REG_HALF,
    STR_REG_IMM_DUAL,
    STR_REG_DUAL,
    STR_HALF_UNPRIV,                            /* Table A5-11 */
    // load instructions
    LD_REG_IMM_HALF = LD_EXTRA_TYPE_BASE,       /* Table A5-10 */
    LD_REG_LIT_HALF,
    LD_REG_HALF,
    LD_REG_IMM_DUAL,
    LD_REG_LIT_DUAL,
    LD_REG_DUAL,
    LD_REG_BYTE_SIGNED,
    LD_REG_IMM_BYTE_SIGNED,
    LD_REG_LIT_BYTE_SIGNED,
    LD_REG_HALF_SIGNED,
    LD_REG_IMM_HALF_SIGNED,
    LD_REG_LIT_HALF_SIGNED,
    LD_HALF_UNPRIV,                             /* Table A5-11 */
    LD_BYTE_SIGNED_UNPRIV,
    LD_HALF_SIGNED_UNPRIV,
} extra_load_store_e;

/*
 * insn_bits are the encoded bits of the instructions
 * instructions that come here matched the check in INSN_IS_EXTRA_LOAD_STORE
 * see ARM ARM table A5-10 and A5-11
 */
static extra_load_store_e decode_extra_load_store(uint32_t insn_bits) {
    extra_load_store_e returnVal = NOT_LOAD_STORE;
    uint8_t op1 = GET_LDST_OP1_BITS(insn_bits);
    uint8_t Rn  = GET_LDST_RN_BITS(insn_bits);
    uint8_t op2 = GET_LDST_EX_OP2_BITS(insn_bits);

    uint8_t op1_mask_1 = op1 & 0x5;
    uint8_t op1_mask_2 = op1 & 0x13;

    switch (op2) {
    case 0x1:           /* halfword */
        // check for some unprivileged extra ld/st first
        if (op1_mask_2 == 0x02) {
            returnVal = STR_HALF_UNPRIV;        /* A8-704 */
            break;
        } else if (op1_mask_2 == 0x03) {
            returnVal = LD_HALF_UNPRIV;         /* A8-448 */
            break;
        }
        // then for non-restricted extra ld/st
        switch (op1_mask_1) {
        case 0x00:
            returnVal = STR_REG_HALF;           /* A8-702 */
            break;
        case 0x01:
            returnVal = LD_REG_HALF;            /* A8-446 */
            break;
        case 0x04:
            returnVal = STR_REG_IMM_HALF;       /* A8-700 */
            break;
        case 0x05:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_HALF;    /* A8-444 */
            } else {
                returnVal = LD_REG_IMM_HALF;    /* A8-442 */
            }
            break;
        // TODO: some unpriv right here?
        default:
            break;
        }
        break;

    case 0x2:           /* load dual and byte */
        if (op1_mask_2 == 0x3) {
            returnVal = LD_BYTE_SIGNED_UNPRIV;          /* A8-456 */
            break;
        }
        switch (op1_mask_1) {
        case 0x00:
            returnVal = LD_REG_DUAL;                    /* A8-430 */
            break;
        case 0x01:
            returnVal = LD_REG_BYTE_SIGNED;             /* A8-454 */
            break;
        case 0x04:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_DUAL;            /* A8-428 */
            } else {
                returnVal = LD_REG_IMM_DUAL;            /* A8-426 */
            }
            break;
        case 0x05:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_BYTE_SIGNED;     /* A8-452 */
            } else {
                returnVal = LD_REG_IMM_BYTE_SIGNED;     /* A8-450 */
            }
            break;
        default:
            break;
        }
        break;

    case 0x3:           /* load signed halfword, store dual */
        if (op1_mask_2 == 0x3) {
            returnVal = LD_HALF_SIGNED_UNPRIV;          /* A8-464 */
            break;
        }
        switch (op1_mask_1) {
        case 0x00:
            returnVal = STR_REG_DUAL;                   /* A8-688 */
            break;
        case 0x01:
            returnVal = LD_REG_HALF_SIGNED;             /* A8-462 */
            break;
        case 0x04:
            returnVal = STR_REG_IMM_DUAL;               /* A8-686 */
            break;
        case 0x05:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_HALF_SIGNED;     /* A8-460 */
            } else {
                returnVal = LD_REG_IMM_HALF_SIGNED;     /* A8-458 */
            }
            break;
        default:
            break;
        }
        break;

    default:
        // it's a different instruction from A5-196
        break;
    }

    return returnVal;
}


// TODO: Synchronization primitives (A5.2.10, page 205) have exclusive loads and stores. do we need to treat these the same? As in, do they use cache?
