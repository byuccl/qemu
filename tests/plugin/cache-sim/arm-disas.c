/*
 * arm-disas.c
 *
 * This file contains functions which make it possible to inspect the
 *  instruction encoding of ARM v7-A 32-bit instructions to determine
 *  1) if they are a load/store
 *  2) what data memory addresses are accessed
 */


/*********************************** notes ************************************/
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
 * TODO: look at these
 * TODO: how to handle conditional loads/stores?
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


/********************************** includes **********************************/
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

#include "arm-disas.h"


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
int INSN_IS_LOAD_STORE(uint32_t insn) {
    uint8_t op1_bits = GET_INSN_OP1_BITS(insn);
    uint8_t op_bit   = GET_INSN_OP_BIT(insn);
    return ( (op1_bits == 0x2) ||                   /* 010 , x */
             ((op1_bits == 0x3) && (op_bit == 0))   /* 011 , 0 */
           );
}


/***************************** regular load/store *****************************/
static const uint32_t LDST_COND_MASK     = CREATE_BIT_MASK(4) << 28;
static const uint32_t LDST_OP1_BITS_MASK = CREATE_BIT_MASK(5) << 20;
static const uint32_t LDST_RN_BITS_MASK  = CREATE_BIT_MASK(4) << 16;
static const uint32_t LDST_A_BIT_MASK    = CREATE_BIT_MASK(1) << 25;
static const uint32_t LDST_B_BIT_MASK    = CREATE_BIT_MASK(1) << 4;
static const uint8_t  LDST_UNPRIV_MASK   = 0x12;    // bits 1 and 4 of 5 total
static const uint8_t  LDST_UNPRIV_BITS   = 0x02;    // only bit 1 is set
static const uint32_t LDST_RT_BITS_MASK  = CREATE_BIT_MASK(4) << 12;
static const uint16_t LDST_IMM12_MASK    = CREATE_BIT_MASK(12);
static const uint16_t LDST_IMM5_MASK     = CREATE_BIT_MASK(5) << 7;
static const uint8_t  LDST_RM_BITS_MASK  = CREATE_BIT_MASK(4);
static const uint8_t  LDST_TYPE_MASK     = CREATE_BIT_MASK(2) << 5;

#define GET_LDST_COND(bits)     ((bits & LDST_COND_MASK) >> 28)
#define GET_LDST_OP1_BITS(bits) ((bits & LDST_OP1_BITS_MASK) >> 20)
#define GET_LDST_RN_BITS(bits)  ((bits & LDST_RN_BITS_MASK) >> 16)
#define GET_LDST_A_BIT(bits)    ((bits & LDST_A_BIT_MASK) >> 25)
#define GET_LDST_B_BIT(bits)    ((bits & LDST_B_BIT_MASK) >> 4)
#define GET_LDST_RT_BITS(bits)  ((bits & LDST_RT_BITS_MASK) >> 12)
#define GET_LDST_IMM12(bits)    ((bits & LDST_IMM12_MASK))
#define GET_LDST_IMM5(bits)     ((bits & LDST_IMM12_MASK) >> 7)
#define GET_LDST_RM_BITS(bits)  ((bits & LDST_RM_BITS_MASK))
#define GET_LDST_TYPE(bits)     ((bits & LDST_TYPE_MASK) >> 5)

typedef enum ldst_enc {
    LDST_INVALID = 0,
    LDST_UNPRIV,
    LDST_IMM,
    LDST_LIT,
    LDST_REG,
} ldst_enc_e;


/*
 * insn_data is a pointer to a struct that will hold data about the instruction
 * insn_bits are the encoded bits of the instruction
 * instructions that come here matched the check in INSN_IS_LOAD_STORE
 * see ARM ARM table A5-15
 */
load_store_e decode_load_store(insn_op_t* insn_data, uint32_t insn_bits) {
    uint8_t op1 = GET_LDST_OP1_BITS(insn_bits);
    uint8_t Rn  = GET_LDST_RN_BITS(insn_bits);
    uint8_t A   = GET_LDST_A_BIT(insn_bits);
    // uint8_t B   = GET_LDST_B_BIT(insn_bits);
    load_store_e returnVal = NOT_LOAD_STORE;
    ldst_enc_e encodeType = LDST_INVALID;

    // store instructions have in common that the lowest bit of op1 is 0
    // load instructions have in common that the lowest bit of op1 is 1
    // for both, if bit 2 of op1 is 1, then it is a byte operation instead of word
    switch (op1 & 0x1) {
    case 0:         /* store */
        switch (op1 & 0x4) {
        case 0x0:   /* word */
            // if bit 1 is 1 and bit 4 is 0, then it's unprivileged
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged store word (A8-706)
                returnVal = STR_REG_UNPRIV;
                encodeType = LDST_UNPRIV;
            } else {
                if (A) {
                    // store register word (A8-676)
                    returnVal = STR_REG;
                    encodeType = LDST_REG;
                } else {
                    // store immediate word (A8-674)
                    returnVal = STR_REG_IMM;
                    encodeType = LDST_IMM;
                }
            }
            break;
        case 0x4:   /* byte */
            // if bit 1 is 1 and bit 4 is 0, then it's unprivileged
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged store byte (A8-684)
                returnVal = STR_REG_BYTE_UNPRIV;
                encodeType = LDST_UNPRIV;
            } else {
                if (A) {
                    // store register byte (A8-682)
                    returnVal = STR_REG_BYTE;
                    encodeType = LDST_REG;
                } else {
                    // store immediate byte (A8-680)
                    returnVal = STR_REG_IMM_BYTE;
                    encodeType = LDST_IMM;
                }
            }
            break;
        }
        break;

    case 1:         /* load  */
        switch (op1 & 0x4) {
        case 0x0:   /* word */
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged load register (A8-466)
                returnVal = LD_REG_UNPRIV;
                encodeType = LDST_UNPRIV;
            } else {
                if (A) {
                    // load register word (A8-414)
                    returnVal = LD_REG;
                    encodeType = LDST_REG;
                } else {
                    if (Rn == 0xF) {
                        // load register literal (A8-410)
                        returnVal = LD_REG_LIT;
                        encodeType = LDST_LIT;
                    } else {
                        // load register immediate (A8-408)
                        returnVal = LD_REG_IMM;
                        encodeType = LDST_IMM;
                    }
                }
            }
            break;
        case 0x4:   /* byte */
            if ((op1 & LDST_UNPRIV_MASK) == LDST_UNPRIV_BITS) {
                // unprivileged load register byte (A8-424)
                returnVal = LD_REG_BYTE_UNPRIV;
                encodeType = LDST_UNPRIV;
            } else {
                if (A) {
                    // load register byte (A8-422)
                    returnVal = LD_REG_BYTE;
                    encodeType = LDST_REG;
                } else {
                    if (Rn == 0xF) {
                        // load register literal byte (A8-420)
                        returnVal = LD_REG_LIT_BYTE;
                        encodeType = LDST_LIT;
                    } else {
                        // load register immediate byte (A8-418)
                        returnVal = LD_REG_IMM_BYTE;
                        encodeType = LDST_IMM;
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

    if (!returnVal)
        return returnVal;

    ///////////////////////// store struct values //////////////////////////
    // sign bit (23)
    insn_data->bitfield.add = (op1 & 0x08);
    // source register, bits 12-15
    insn_data->bitfield.Rt = GET_LDST_RT_BITS(insn_bits);
    // destination register, already computed
    insn_data->bitfield.Rn = Rn;
    // condition codes
    insn_data->bitfield.cond = GET_LDST_COND(insn_bits);
    // bit 24
    uint8_t P = op1 & 0x10;
    uint8_t index = (P == 0x1);
    uint8_t wback = ( (P == 0x0) || ((op1 & 0x2) == 0x2) );

    switch (encodeType) {
    case LDST_UNPRIV:
        // if bit 25 is set, then it's A2, otherwise, A1
        if (A) {
            insn_data->imm.imm5 = GET_LDST_IMM5(insn_bits);
            insn_data->bitfield.Rm = GET_LDST_RM_BITS(insn_bits);
            insn_data->bitfield.type = GET_LDST_TYPE(insn_bits);
        } else {
            insn_data->imm.imm12 = GET_LDST_IMM12(insn_bits);
        }
        break;
    case LDST_REG:
        insn_data->imm.imm5 = GET_LDST_IMM5(insn_bits);
        insn_data->bitfield.Rm = GET_LDST_RM_BITS(insn_bits);
        insn_data->bitfield.type = GET_LDST_TYPE(insn_bits);
        insn_data->bitfield.index = index;
        insn_data->bitfield.wback = wback;
        break;
    case LDST_IMM:
        insn_data->imm.imm12 = GET_LDST_IMM12(insn_bits);
        insn_data->bitfield.index = index;
        insn_data->bitfield.wback = wback;
        break;
    case LDST_LIT:
        // this is zero extended to be 32 bits
        insn_data->imm.imm32 = GET_LDST_IMM12(insn_bits);
        break;
    default:
        break;
    }

    // record the type
    insn_data->type.load_store = returnVal;

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
// #define MISC_IS_SYNC_PRIMITIVE      (3)     /* A5-205 */

/*
 * See table A5-2
 * TODO: this can probably be simplified
 */
int INSN_IS_EXTRA_LOAD_STORE(uint32_t insn) {
    // first check if the op1 bits are correct
    uint8_t arm_op1_bits = GET_INSN_OP1_BITS(insn);
    // only the bottom bit can be set
    if (arm_op1_bits > 0x1) {
        return 0;
    }

    uint8_t op_bit   = GET_MISC_OP_BIT(insn);
    uint8_t op1_bits = GET_MISC_OP1_BITS(insn);
    uint8_t op2_bits = GET_MISC_OP2_BITS(insn);
    int returnVal = 0;

    if (op_bit == 0) {
        uint8_t op1_mask_1 = op1_bits & 0x12;
        uint8_t op2_mask_1 = op2_bits & 0xD;
        uint8_t op1_mask_2 = op1_bits & 0x13;
        // we're also going to piggyback this function to look for
        //  synchronization primitives
        uint8_t op1_mask_3 = op1_bits & 0x10;

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
        else if ( (op1_mask_3 == 0x10) &&
                    (op2_bits == 0x9))
        {
            returnVal = MISC_IS_SYNC_PRIMITIVE;
        }
    }

    return returnVal;
}


/************************** decode extra load/store ***************************/
// op1 and Rn are the same as the regular ld/st instructions
static const uint32_t LDST_EX_OP2_BITS_MASK = CREATE_BIT_MASK(2) << 5;
static const uint32_t LDST_EX_IMM_H_MASK    = CREATE_BIT_MASK(4) << 8;
static const uint32_t LDST_EX_IMM_L_MASK    = CREATE_BIT_MASK(4);

#define GET_LDST_EX_OP2_BITS(bits) ((bits & LDST_EX_OP2_BITS_MASK) >> 5)
#define GET_LDST_EX_IMM_H(bits)    ((bits & LDST_EX_IMM_H_MASK) >> 4)   // yes, 4
#define GET_LDST_EX_IMM_L(bits)    ((bits & LDST_EX_IMM_L_MASK))
#define GET_LDST_EX_IMM_BITS(bits) (GET_LDST_EX_IMM_H(bits) | GET_LDST_EX_IMM_L(bits))

/*
 * insn_bits are the encoded bits of the instructions
 * instructions that come here matched the check in INSN_IS_EXTRA_LOAD_STORE
 * see ARM ARM table A5-10 and A5-11
 */
extra_load_store_e decode_extra_load_store(insn_op_t* insn_data, uint32_t insn_bits) {
    extra_load_store_e returnVal = NOT_LOAD_STORE;
    uint8_t op1 = GET_LDST_OP1_BITS(insn_bits);
    uint8_t Rn  = GET_LDST_RN_BITS(insn_bits);
    uint8_t op2 = GET_LDST_EX_OP2_BITS(insn_bits);
    ldst_enc_e encodeType = LDST_INVALID;

    uint8_t op1_mask_1 = op1 & 0x5;
    uint8_t op1_mask_2 = op1 & 0x13;

    switch (op2) {
    case 0x1:           /* halfword */
        // check for some unprivileged extra ld/st first
        if (op1_mask_2 == 0x02) {
            returnVal = STR_HALF_UNPRIV;        /* A8-704 */
            encodeType = LDST_UNPRIV;
            break;
        } else if (op1_mask_2 == 0x03) {
            returnVal = LD_HALF_UNPRIV;         /* A8-448 */
            encodeType = LDST_UNPRIV;
            break;
        }
        // then for non-restricted extra ld/st
        switch (op1_mask_1) {
        case 0x00:
            returnVal = STR_REG_HALF;           /* A8-702 */
            encodeType = LDST_REG;
            break;
        case 0x01:
            returnVal = LD_REG_HALF;            /* A8-446 */
            encodeType = LDST_REG;
            break;
        case 0x04:
            returnVal = STR_REG_IMM_HALF;       /* A8-700 */
            encodeType = LDST_IMM;
            break;
        case 0x05:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_HALF;    /* A8-444 */
                encodeType = LDST_LIT;
            } else {
                returnVal = LD_REG_IMM_HALF;    /* A8-442 */
                encodeType = LDST_IMM;
            }
            break;
        default:
            break;
        }
        break;

    case 0x2:           /* load dual and byte */
        if (op1_mask_2 == 0x3) {
            returnVal = LD_BYTE_SIGNED_UNPRIV;          /* A8-456 */
            encodeType = LDST_UNPRIV;
            break;
        }
        switch (op1_mask_1) {
        case 0x00:
            returnVal = LD_REG_DUAL;                    /* A8-430 */
            encodeType = LDST_REG;
            break;
        case 0x01:
            returnVal = LD_REG_BYTE_SIGNED;             /* A8-454 */
            encodeType = LDST_REG;
            break;
        case 0x04:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_DUAL;            /* A8-428 */
            } else {
                returnVal = LD_REG_IMM_DUAL;            /* A8-426 */
                encodeType = LDST_IMM;
            }
            break;
        case 0x05:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_BYTE_SIGNED;     /* A8-452 */
                encodeType = LDST_LIT;
            } else {
                returnVal = LD_REG_IMM_BYTE_SIGNED;     /* A8-450 */
                encodeType = LDST_IMM;
            }
            break;
        default:
            break;
        }
        break;

    case 0x3:           /* load signed halfword, store dual */
        if (op1_mask_2 == 0x3) {
            returnVal = LD_HALF_SIGNED_UNPRIV;          /* A8-464 */
            encodeType = LDST_UNPRIV;
            break;
        }
        switch (op1_mask_1) {
        case 0x00:
            returnVal = STR_REG_DUAL;                   /* A8-688 */
            encodeType = LDST_REG;
            break;
        case 0x01:
            returnVal = LD_REG_HALF_SIGNED;             /* A8-462 */
            encodeType = LDST_REG;
            break;
        case 0x04:
            returnVal = STR_REG_IMM_DUAL;               /* A8-686 */
            encodeType = LDST_IMM;
            break;
        case 0x05:
            if (Rn == 0xF) {
                returnVal = LD_REG_LIT_HALF_SIGNED;     /* A8-460 */
                encodeType = LDST_LIT;
            } else {
                returnVal = LD_REG_IMM_HALF_SIGNED;     /* A8-458 */
                encodeType = LDST_IMM;
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

    if (!returnVal)
        return returnVal;

    ///////////////////////// store struct values //////////////////////////
    // sign bit (23)
    insn_data->bitfield.add = (op1 & 0x08);
    // source register, bits 12-15
    insn_data->bitfield.Rt = GET_LDST_RT_BITS(insn_bits);
    // destination register, already computed
    insn_data->bitfield.Rn = Rn;
    // condition codes
    insn_data->bitfield.cond = GET_LDST_COND(insn_bits);

    uint8_t P = op1 & 0x10;
    uint8_t index = (P == 0x1);
    uint8_t wback = ( (P == 0x0) || ((op1 & 0x2) == 0x2) );

    switch (encodeType) {
    case LDST_UNPRIV:
        // if bit 22 is set, encoding type A1, else encoding type A2
        if (op1 & 0x04) {
            // get immediate bits, zero extend
            insn_data->imm.imm32 = GET_LDST_EX_IMM_BITS(insn_bits);
        } else {
            // get Rm
            insn_data->bitfield.Rm = GET_LDST_RM_BITS(insn_bits);
        }
        break;
    case LDST_IMM:
        insn_data->imm.imm32 = GET_LDST_EX_IMM_BITS(insn_bits);
        insn_data->bitfield.index = index;
        insn_data->bitfield.wback = wback;
        break;
    case LDST_REG:
        insn_data->bitfield.Rm = GET_LDST_RM_BITS(insn_bits);
        insn_data->bitfield.index = index;
        insn_data->bitfield.wback = wback;
        break;
    case LDST_LIT:
        insn_data->imm.imm32 = GET_LDST_EX_IMM_BITS(insn_bits);
        break;
    default:
        break;
    }

    insn_data->type.extra_load_store = returnVal;

    return returnVal;
}


/*************************** check block load/store ***************************/
int INSN_IS_BLOCK_LOAD_STORE(uint32_t insn) {
    uint8_t op1_bits = GET_INSN_OP1_BITS(insn);
    uint8_t op1_masked = op1_bits & 0x6;    // don't care about bit 0
    return (op1_masked == 0x4);
}


/************************** decode block load/store ***************************/
static const uint32_t LDSTM_OP_BITS_MASK = CREATE_BIT_MASK(6) << 20;
static const uint32_t LDSTM_RN_BITS_MASK = CREATE_BIT_MASK(4) << 16;
static const uint32_t LDSTM_R_BIT_MASK   = CREATE_BIT_MASK(1) << 15;
static const uint32_t LDSTM_REGLIST_MASK = CREATE_BIT_MASK(16);

#define GET_LDSTM_OP_BITS(bits) ((bits & LDSTM_OP_BITS_MASK) >> 20)
#define GET_LDSTM_RN_BITS(bits) ((bits & LDSTM_RN_BITS_MASK) >> 16)
#define GET_LDSTM_R_BIT(bits)   ((bits & LDSTM_R_BIT_MASK) >> 15)
#define GET_LDSTM_REGLIST(bits) ((bits & LDSTM_REGLIST_MASK))

/*
 * Load/store a block of memory all at once.
 * How these instructions encode a registers list is defined in
 *  section A8.6, page 295
 */
block_load_store_e decode_block_load_store(insn_op_t* insn_data, uint32_t insn_bits) {
    uint8_t op = GET_LDSTM_OP_BITS(insn_bits);
    uint8_t Rn = GET_LDSTM_RN_BITS(insn_bits);
    uint8_t exc_flag = 0;
    block_load_store_e returnVal = NOT_BLK_LOAD_STORE;

    switch (op) {
    case 0x00:  case 0x02:
        returnVal = STRM_DEC_AFT;           /* A8-666 */
        break;
    case 0x01:  case 0x03:
        returnVal = LDM_DEC_AFT;            /* A8-400 */
        break;
    case 0x08:  case 0xA:
        returnVal = STRM_INC_AFT;           /* A8-664 */
        break;
    case 0x09:
        returnVal = LDM_INC_AFT;            /* A8-398 */
        break;
    case 0x0B:
        if (Rn == 0xD) {
            returnVal = POP_MULT;           /* A8-536 */
        } else {
            returnVal = LDM_INC_AFT;        /* A8-398 */
        }
        break;
    case 0x10:
        returnVal = STRM_DEC_BEF;           /* A8-668 */
        break;
    case 0x12:
        if (Rn == 0xD) {
            returnVal = PUSH_MULT;          /* A8-538 */
        } else {
            returnVal = STRM_DEC_BEF;       /* A8-668 */
        }
        break;
    case 0x11:  case 0x13:
        returnVal = LDM_DEC_BEF;            /* A8-402 */
        break;
    case 0x18:  case 0x1A:
        returnVal = STRM_INC_BEF;           /* A8-670 */
        break;
    case 0x19:  case 0x1B:
        returnVal = LDM_INC_BEF;            /* A8-404 */
        break;
    default:
        // now more generic matches
        if ((op & 0x05) == 0x04) {
            returnVal = STRM_USR_REG;       /* B9-2008 */
            exc_flag = 1;
        } else if ((op & 0x05) == 0x05) {
            uint8_t R  = GET_LDSTM_R_BIT(insn_bits);
            if (R) {
                returnVal = LDM_EXC_RET;    /* B9-1986 */
                exc_flag = 1;
            } else {
                returnVal = LDM_USR_REG;    /* B9-1988 */
                exc_flag = 1;
            }
        }
    }

    if (!returnVal)
        return returnVal;

    ///////////////////////// store struct values //////////////////////////
    insn_data->bitfield.wback = ((op & 0x2) == 0x2);
    insn_data->bitfield.Rn = Rn;
    insn_data->imm.regList = GET_LDSTM_REGLIST(insn_bits);
    // condition codes
    insn_data->bitfield.cond = GET_LDST_COND(insn_bits);
    insn_data->type.load_store = returnVal;

    // do extra stuff for USR_REG and EXC_RET
    if (exc_flag) {
        // TODO: we should unpack here, not later
        insn_data->bitfield.type = op;
    }

    return returnVal;
}


/***************************** coprocessor ld/st ******************************/
static const uint32_t COPROC_BITS_MASK = CREATE_BIT_MASK(4) << 8;
static const uint16_t COPROC_IMM8_MASK = CREATE_BIT_MASK(8);

// reuse a bitmask
#define GET_COPROC_OP1_BITS(bits)  ((bits & LDSTM_OP_BITS_MASK) >> 20)
#define GET_COPROC_RN_BITS(bits)   ((bits & LDSTM_RN_BITS_MASK) >> 16)
#define GET_COPROC_CP_BITS(bits)   ((bits & COPROC_BITS_MASK) >> 8)
#define GET_COPROC_IMM8_BITS(bits) ((bits & COPROC_IMM8_MASK))
#define GET_COPROC_RD_BITS(bits)   ((bits & LDST_RT_BITS_MASK) >> 12)

/*
 * Table A5-22 on page A5-215
 * TODO: this has not yet been tested
 */
cp_load_store_e INSN_IS_COPROC_LOAD_STORE(insn_op_t* insn_data, uint32_t insn) {
    // don't care about bit 0
    uint8_t arm_op1 = GET_INSN_OP1_BITS(insn) & 0x6;
    cp_load_store_e returnVal = NOT_CP_LOAD_STORE;
    uint8_t op1_bits, cp_bits;

    if (arm_op1 == 0x6) {
        // mask out bit 2
        op1_bits = GET_COPROC_OP1_BITS(insn) & 0x3B;
        // mask out bit 0
        cp_bits = GET_COPROC_CP_BITS(insn) & 0xE;

        if (cp_bits != 0xA) {
            // store is 0xxxx0 not 000x00
            //  load is 0xxxx1 not 000x01
            if (op1_bits) {
                // is it potentially a load?
                if (op1_bits & 0x1) {
                    if (op1_bits > 1) {
                        uint8_t Rn = GET_COPROC_RN_BITS(insn);
                        if (Rn == 0xF) {
                            returnVal = CP_LD_LIT;      /* A8-394 */
                        } else {
                            returnVal = CP_LD_IMM;      /* A8-392 */
                        }
                    }
                } else {
                    returnVal = CP_STR;                 /* A8-662 */
                }
            }
        }
    }

    if (!returnVal)
        return returnVal;

    ///////////////////////// store struct values //////////////////////////
    // this can be ignored when literal
    insn_data->bitfield.Rn = GET_LDST_RN_BITS(insn);
    // zero-extended 8-bit immediate
    insn_data->imm.imm32 = GET_COPROC_IMM8_BITS(insn);
    // CRd
    insn_data->bitfield.Rd = GET_COPROC_RD_BITS(insn);
    // other common things
    insn_data->bitfield.cond = GET_LDST_COND(insn);
    insn_data->bitfield.coproc = cp_bits;
    uint8_t P = arm_op1 & 0x10;
    insn_data->bitfield.index = (P == 0x1);
    insn_data->bitfield.wback = ( (P == 0x0) || ((arm_op1 & 0x2) == 0x2) );
    insn_data->type.load_store = returnVal;

    return returnVal;
}


/************************* synchronization primitives *************************/
static const uint32_t SYNC_OP_BITS_MASK = CREATE_BIT_MASK(4) << 20;

#define GET_SYNC_OP_BITS(insn)  ((insn & SYNC_OP_BITS_MASK) >> 20)
#define GET_LDST_SYNC_RD(bits)  ((bits & LDST_RT_BITS_MASK) >> 12)
#define GET_LDST_SYNC_RT(bits)  ((bits & LDST_RM_BITS_MASK))
#define GET_LDST_SYNC_RT2(bits) ((bits & LDST_RM_BITS_MASK))

/*
 * Synchronization primitive instructions
 */
sync_load_store_e decode_sync_load_store(insn_op_t* insn_data, uint32_t insn) {
    uint8_t op_bits = GET_SYNC_OP_BITS(insn);
    sync_load_store_e returnVal = NOT_SYNC_LOAD_STORE;

    switch (op_bits) {
    case 0x0:
        returnVal = SWAP_WORD;          /* A8-722 */
        break;
    case 0x4:
        returnVal = SWAP_BYTE;          /* A8-722 */
        break;
    case 0x8:
        returnVal = STR_EXCL;           /* A8-690 */
        break;
    case 0x9:
        returnVal = LD_EXCL;            /* A8-432 */
        break;
    case 0xA:
        returnVal = STR_EXCL_DW;        /* A8-694 */
        break;
    case 0xB:
        returnVal = LD_EXCL_DW;         /* A8-436 */
        break;
    case 0xC:
        returnVal = STR_EXCL_BYTE;      /* A8-692 */
        break;
    case 0xD:
        returnVal = LD_EXCL_BYTE;       /* A8-434 */
        break;
    case 0xE:
        returnVal = STR_EXCL_HALF;      /* A8-696 */
        break;
    case 0xF:
        returnVal = LD_EXCL_HALF;       /* A8-438 */
        break;
    default:
        break;
    }

    ///////////////////////// store struct values //////////////////////////
    insn_data->bitfield.Rn = GET_LDST_RN_BITS(insn);
    // condition codes
    insn_data->bitfield.cond = GET_LDST_COND(insn);

    if (returnVal >= SWAP_WORD) {
        // swap type - deprecated by ARM!
        insn_data->bitfield.Rt = GET_LDST_RT_BITS(insn);
        insn_data->bitfield.Rt2 = GET_LDST_SYNC_RT2(insn);
    } else if (returnVal < LD_SYNC_TYPE_BASE) {
        // store type
        insn_data->bitfield.Rt = GET_LDST_SYNC_RT(insn);
        insn_data->bitfield.Rd = GET_LDST_SYNC_RD(insn);
    } else {
        // load type
        insn_data->bitfield.Rt = GET_LDST_RT_BITS(insn);
    }

    return returnVal;
}


/******************************** vector ld/st ********************************/
// TODO
