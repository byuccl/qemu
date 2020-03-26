#ifndef __ARM_DISAS_H
#define __ARM_DISAS_H
// include guard

#include <stdint.h>

/*
 * Header file for disassembling ARM instructions.
 */


/**************************** determine load/store ****************************/
int INSN_IS_LOAD_STORE(uint32_t insn);


/***************************** regular load/store *****************************/
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

load_store_e decode_load_store(insn_op_t* insn_data, uint32_t insn_bits);


/************************* determine extra load/store *************************/
#define MISC_IS_SYNC_PRIMITIVE      (3)     /* A5-205 */
int INSN_IS_EXTRA_LOAD_STORE(uint32_t insn);


/************************** decode extra load/store ***************************/
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

extra_load_store_e decode_extra_load_store(uint32_t insn_bits);


/*************************** check block load/store ***************************/
int INSN_IS_BLOCK_LOAD_STORE(uint32_t insn);


/************************** decode block load/store ***************************/
#define STR_BLK_TYPE_BASE 0x2001
#define LD_BLK_TYPE_BASE 0x2100
typedef enum block_load_store {
    NOT_BLK_LOAD_STORE = 0,
    // store instructions
    STRM_DEC_AFT = STR_BLK_TYPE_BASE,
    STRM_DEC_BEF,
    STRM_INC_AFT,
    STRM_INC_BEF,
    STRM_USR_REG,
    PUSH_MULT,
    // load instructions
    LDM_DEC_AFT = LD_TYPE_BASE,
    LDM_DEC_BEF,
    LDM_INC_AFT,
    LDM_INC_BEF,
    LDM_USR_REG,
    LDM_EXC_RET,
    POP_MULT,
} block_load_store_e;

block_load_store_e decode_block_load_store(uint32_t insn_bits);


/***************************** coprocessor ld/st ******************************/
#define STR_CP_TYPE_BASE 0x3001
#define LD_CP_TYPE_BASE  0X3100

typedef enum cp_load_store {
    NOT_CP_LOAD_STORE = 0,
    CP_STR = STR_CP_TYPE_BASE,
    CP_LD_IMM = LD_CP_TYPE_BASE,
    CP_LD_LIT,
} cp_load_store_e;

cp_load_store_e INSN_IS_COPROC_LOAD_STORE(uint32_t insn);


/************************* synchronization primitives *************************/
#define STR_SYNC_TYPE_BASE 0x4001
#define LD_SYNC_TYPE_BASE  0x4100
#define SWP_SYNC_TYPE_BASE 0x4200

typedef enum sync_load_store {
    NOT_SYNC_LOAD_STORE = 0,
    STR_EXCL = STR_SYNC_TYPE_BASE,
    STR_EXCL_DW,
    STR_EXCL_BYTE,
    STR_EXCL_HALF,
    LD_EXCL = LD_SYNC_TYPE_BASE,
    LD_EXCL_DW,
    LD_EXCL_BYTE,
    LD_EXCL_HALF,
    SWAP_WORD = SWP_SYNC_TYPE_BASE,
    SWAP_BYTE,
} sync_load_store_e;

sync_load_store_e decode_sync_load_store(uint32_t insn);


/******************************** vector ld/st ********************************/
// TODO


#endif  /* __ARM_DISAS_H */
