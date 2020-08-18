#ifndef __ARM_DISAS_H
#define __ARM_DISAS_H
// include guard

#include <stdint.h>

/*
 * Header file for disassembling ARM instructions.
 * TODO: for determing which type things are, we can mask out the bottom byte,
 *  and use the upper half for range matching (switch)
 */


/**************************** determine load/store ****************************/
int INSN_IS_LOAD_STORE(uint32_t insn);


/***************************** regular load/store *****************************/
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


/***************************** coprocessor ld/st ******************************/
#define STR_CP_TYPE_BASE 0x3001
#define LD_CP_TYPE_BASE  0x3100
#define CP_REG_TYPE_BASE 0x3200

typedef enum cp_load_store {
    NOT_CP_LOAD_STORE = 0,
    CP_STR = STR_CP_TYPE_BASE,
    CP_LD_IMM = LD_CP_TYPE_BASE,
    CP_LD_LIT,
    CP_MCR = CP_REG_TYPE_BASE,
    CP_MRC,
} cp_load_store_e;


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


/******************************** vector ld/st ********************************/
// TODO


/*************************** instruction parameters ***************************/
// TODO: use unions for things that can overlap
struct insn_op {
    uint32_t dataAddr;
    struct bit_field_s {
        uint8_t cond;
        uint8_t Rn;
        uint8_t Rt;
        uint8_t Rt2;    // also known as opc2
        uint8_t Rm;
        uint8_t Rd;     // also known as CRd
        uint8_t type;   // also the opcode for LDM_EXC and MCR
        uint8_t add;
        uint8_t index;
        uint8_t wback;
        uint8_t coproc;
    } bitfield;
    // you can only have one immediate field in any instruction
    union imm_u {
        uint32_t imm32;     // this is comes from zero-extending something smaller
        uint16_t imm12;
        uint8_t imm5;
        uint8_t imm8;
        uint16_t regList;   // bit-encoded register list (pop, push, ldm, stm, etc)
    } imm;
    // what is the kind of instruction
    union type_u {
        load_store_e load_store;
        extra_load_store_e extra_load_store;
        block_load_store_e block_load_store;
        cp_load_store_e cp_load_store;
        sync_load_store_e sync_load_store;
    } type;
};
typedef struct insn_op insn_op_t;


/**************************** function prototypes *****************************/

load_store_e decode_load_store(insn_op_t* insn_data, uint32_t insn_bits);
extra_load_store_e decode_extra_load_store(insn_op_t* insn_data, uint32_t insn_bits);
block_load_store_e decode_block_load_store(insn_op_t* insn_data, uint32_t insn_bits);
cp_load_store_e INSN_IS_COPROC_LOAD_STORE(insn_op_t* insn_data, uint32_t insn);
sync_load_store_e decode_sync_load_store(insn_op_t* insn_data, uint32_t insn);


#endif  /* __ARM_DISAS_H */
