/*
 * Qemu plugin to track load and store instructions.
 * Simulates processor caches.
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

#include "icache.h"
#include "dcache.h"
#include "l2cache.h"
#include "arm-disas.h"

// required export for it to build properly
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/******************************** definitions *********************************/
// have available the disassembled string when debugging the callbacks
// #define DEBUG_INSN_DISAS
#ifdef DEBUG_INSN_DISAS
#define LAST_INSN_BUF_SIZE 64
#endif

// hack - get register values
#define SIZE_OF_CPUState 33480
#define SIZE_OF_CPUNegativeOffsetState 3632
// thanks - https://stackoverflow.com/a/53884709/12940429
#define CPU_STRUCT_OFFSET (SIZE_OF_CPUState + SIZE_OF_CPUNegativeOffsetState + 8)


/**************************** function prototypes *****************************/
static void parse_instruction(unsigned int vcpu_index, void* userdata);
static void parse_mem(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                        uint64_t vaddr, void* userdata);
static void cache_inst(unsigned int vcpu_index, void* userdata);
static void icache_inst(unsigned int vcpu_index, void* userdata);
static void plugin_exit(qemu_plugin_id_t id, void* p);
static void put_cbs_in_tbs(qemu_plugin_id_t id, struct qemu_plugin_tb* tb);

// hack - https://stackoverflow.com/a/61977875/12940429
void *qemu_get_cpu(int index);
static uint32_t get_cpu_register(unsigned int cpu_index, unsigned int reg);


/********************************** globals ***********************************/
static uint64_t insn_count = 0;
static uint64_t load_count = 0;
static uint64_t store_count = 0;
static uint64_t cp_count = 0;
static uint64_t textBegin = 0, textEnd = 0;     // begin and end addresses of .text
static gboolean is_arm_arch = FALSE;

#ifdef DEBUG_INSN_DISAS
char lastInsnStr[LAST_INSN_BUF_SIZE];
#endif


/************************* Cache Control Instructions *************************/
static const uint32_t COPROC_RT_BITS = CREATE_BIT_MASK(4) << 12;
static const uint32_t WAY_BITS = CREATE_BIT_MASK(2) << 30;
static const uint32_t SET_BITS = CREATE_BIT_MASK(10) << 4;

#define GET_COPROC_RT_BITS(bits)    ((bits & COPROC_RT_BITS) >> 12)
#define GET_WAY_BITS(bits)          ((bits & WAY_BITS) >> 30)
#define GET_SET_BITS(bits)          ((bits & SET_BITS) >> 4)


/********************************* functions **********************************/

/*
 * Get the value of a register in the current CPU state.
 * This function is a hack and not officially supported by the plugin interface.
 */
static uint32_t get_cpu_register(unsigned int cpu_index, unsigned int reg) {
    uint8_t* cpu = qemu_get_cpu(cpu_index);
    return *(uint32_t*)( cpu + CPU_STRUCT_OFFSET + (reg * 4) );
}


/* 
 * get the encoded bits as a single word of arch size
 * TODO: Thumb, and any other architecture
 */
static arch_word_t get_insn_bits(struct qemu_plugin_insn* insn) {
    // data is a pointer to a GByteArray
    // https://developer.gnome.org/glib/stable/glib-Byte-Arrays.html#GByteArray
    const void* insn_data = qemu_plugin_insn_data(insn);
    size_t insn_size = qemu_plugin_insn_size(insn);
    if (insn_size != 4) {
        // error, we expected only ARM 32-bit instruction, no aarch64 or thumb
    }

    const guint8* data_array = (guint8*)insn_data;
    arch_word_t insn_bits = 0;
    int i;
    for (i = insn_size - 1; i >= 0; i--) {
        insn_bits |= (data_array[i] << (i * 8));
    }

    return insn_bits;
}

/*
 * Will register a callback with each instruction executed
 * Based on code in insn.c, hotpages.c, and mem.c plugins
 */
static void put_cbs_in_tbs(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {

    // get the number of instructions in this tb
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    // iterate over each instruction and register a callback with it
    for (i = 0; i < n; i++) {
        // get the handle for the instruction
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);

        // we need to pass some data about the instruction to the callback
        // get the virtual address of the instruction, which is what shows
        //  up in GDB debugging the guest program
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);

#ifdef DEBUG_INSN_DISAS
        // get the disassembly of the instruction
        char* disas_str = qemu_plugin_insn_disas(insn);
        strncpy(lastInsnStr, disas_str, LAST_INSN_BUF_SIZE);
#endif

        if ( (insn_vaddr < textEnd) && (insn_vaddr >= textBegin) ) {
            // register a callback for everything else to track icache uses
            qemu_plugin_register_vcpu_insn_exec_cb(
                                        insn, parse_instruction,
                                        QEMU_PLUGIN_CB_NO_REGS,
                                        (void*)insn_vaddr);
        }
        // watch for data loads/stores
        qemu_plugin_register_vcpu_mem_cb(insn, parse_mem,
                                        QEMU_PLUGIN_CB_NO_REGS,
                                        QEMU_PLUGIN_MEM_RW, NULL);

        if (!is_arm_arch) {
            continue;
        }
        /////////////////// Follows ARM Architecture ///////////////////
        //////////////////// Specific Functionality ////////////////////

        arch_word_t insn_bits = get_insn_bits(insn);

        // also have to see if there are any instructions that invalidate the cache
        insn_op_t insn_op_data;
        cp_load_store_e cp_type = INSN_IS_COPROC_LOAD_STORE(&insn_op_data, insn_bits);
        if (cp_type >= CP_REG_TYPE_BASE) {

            if ( cp_type == CP_MCR )
            {
                // controlling dcache?
                if (dcache_is_cache_inst(&insn_op_data))
                {
                    qemu_plugin_register_vcpu_insn_exec_cb(insn, cache_inst,
                                                QEMU_PLUGIN_CB_R_REGS,
                                                (void*) (uint64_t) insn_bits);
                    // have to cast to large enough int type before cast to pointer
                }
                // controlling icache?
                else if (icache_is_cache_inst(&insn_op_data))
                {
                    qemu_plugin_register_vcpu_insn_exec_cb(insn, icache_inst,
                                                QEMU_PLUGIN_CB_NO_REGS, NULL);
                }
            }
        }
    }
}


/*
 * Function is called every time a non-memory instruction is executed.
 */
static void parse_instruction(unsigned int vcpu_index, void* userdata)
{
    insn_count += 1;
    icache_load((uint64_t)userdata);
}


/*
 * Function is called every time a load or store instruction is executed.
 */
static void parse_mem(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                        uint64_t vaddr, void* userdata)
{
    struct qemu_plugin_hwaddr* hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    uint64_t addr;

    // get the address (see hotpages.c)
    if (hwaddr && !qemu_plugin_hwaddr_is_io(hwaddr)) {
        addr = (uint64_t) qemu_plugin_hwaddr_device_offset(hwaddr);
    } else {
        addr = vaddr;
    }

    // skip instruction loads, already taken care of
    if ( (addr < textEnd) && (addr >= textBegin) ) {
        return;
    }

    // do the right thing
    if (qemu_plugin_mem_is_store(info)) {
        store_count += 1;
        dcache_store(addr);
    } else {
        load_count += 1;
        dcache_load(addr);
    }
    // TODO: swaps?
}


/*
 * Execute an instruction which changes the state of the cache.
 * We have to change the state of the in-memory model of the cache.
 * Looking for something like this:
 *    mcr	p15, 0, r11, c7, c6, 2
 * https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L448
 *
 * Executes the "DCISW" pseudo-instruction
 * https://developer.arm.com/docs/ddi0601/f/aarch32-system-instructions/dcisw
 * SetWay, bits [31:4]; Level, bits [3:1], Bit [0] reserved
 * Way, bits[31:32-A], the number of the way to operate on.
 * Set, bits[B-1:L], the number of the set to operate on.
 * So Way is bits [31:30] (because A = log_2(Associativity) = log_2(4) = 2)
 * L = log_2(LINELEN) = log_2(32) = 5
 * S = log_2(NSETS) = log_2(32k/32/4) = log_2(512) = 9
 * B = (L+S) = 5+9 = 14
 * And Set is bits [13:4]
 */
static void cache_inst(unsigned int vcpu_index, void* userdata)
{

    // Pass the instruction bits in literally
    arch_word_t insn_bits = (uint64_t) userdata;

    // The register being read from are bits 12-15
    // See arm-disas.c:INSN_IS_COPROC_LOAD_STORE for more information
    unsigned int regIdx = (unsigned int) GET_COPROC_RT_BITS(insn_bits);
    // Read the register
    uint32_t readRt = get_cpu_register(vcpu_index, regIdx);
    int readSet = GET_SET_BITS(readRt);
    int readWay = GET_WAY_BITS(readRt);

    // ARM "set" == my "row"
    // ARM "way" == my "way"

    // debug printing
    /* g_autoptr(GString) out = g_string_new("");
     * g_string_printf(out, "cycle: %lu, Rt: 0x%08x\n", insn_count, readRt);
     * g_string_append_printf(out, "  set: %#x, way: %#x\n", readSet, readWay);
     * qemu_plugin_outs(out->str);
     */

    // invalidate a specific cache row and block
    dcache_invalidate_block(readSet, readWay);

    cp_count += 1;
}


/*
 * There is a single instruction that invalides the entire icache
 * https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L224
 *    mcr	p15, 0, r0, c7, c5, 0
 * Executes the "ICIALLU" pseudo-instruction
 * https://developer.arm.com/docs/ddi0595/h/aarch32-system-instructions/iciallu
 * Don't even need to read the register.
 */
static void icache_inst(unsigned int vcpu_index, void* userdata)
{
    icache_invalidate_all();
    cp_count += 1;
}

/*
 * Register the plugin.
 * This is kind of like "main".
 * Arguments:
 *  textBegin - first address in the address space of instruction memory
 *  textEnd   - last address in the address space of instruction memory
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                            const qemu_info_t* info,
                                            int argc, char** argv)
{
    // parse arguments to the plugin
    // argv[0] is NOT the name of the program, like normal

    // allow all args or none
    uint32_t numArgs = 2;
    if (argc != numArgs)
    {
        qemu_plugin_outs("Wrong number of arguments to plugin!\n");
        return !0;
    }

    // based on example in howvec.c
    for (int i = 0; i < argc; i++) {
        char* p = argv[i];
        switch (i) {
            case 0:
                textBegin = strtoul(p, NULL, 16);
                break;
            case 1:
                textEnd = strtoul(p, NULL, 16);
                break;
            default:
                qemu_plugin_outs("Too many input arguments to plugin!\n");
                return !0;
        }
    }

    // record if we can use target specific functions
    if (strcmp(info->target_name, "arm") == 0) {
        is_arm_arch = TRUE;
    }

    // init the cache simulation
    icache_init(ICACHE_SIZE_BYTES, ICACHE_ASSOCIATIVITY,
                ICACHE_BLOCK_SIZE, ICACHE_POLICY);
    dcache_init(DCACHE_SIZE_BYTES, DCACHE_ASSOCIATIVITY,
                DCACHE_BLOCK_SIZE, DCACHE_POLICY);
    l2cache_init(L2CACHE_SIZE_BYTES, L2CACHE_ASSOCIATIVITY,
                L2CACHE_BLOCK_SIZE, L2CACHE_POLICY);

    // `info` argument has information about qemu system state
    // see qemu_info_t in include/qemu/qemu-plugin.h for more details

    // register the functions in this file
    qemu_plugin_register_vcpu_tb_trans_cb(id, put_cbs_in_tbs);

    // to be run at exit
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    // print status - debug
    /*
     * g_autoptr(GString) out = g_string_new("");
     * g_string_printf(out, "Initializing...\n");
     * g_string_append_printf(out, "text: 0x%lX - 0x%lX\n", textBegin, textEnd);
     * g_string_append_printf(out, "target: %s\n", info->target_name);
     * qemu_plugin_outs(out->str);
    */

    return 0;
}


/*
 * Called when the plugin is to be removed (atexit)
 */
static void plugin_exit(qemu_plugin_id_t id, void* p) {
    // based on example in mem.c
    g_autoptr(GString) out = g_string_new("");

    g_string_printf(out,        "insn count:           %10ld\n", insn_count);
    g_string_append_printf(out, "load count:           %10ld\n", load_count);
    g_string_append_printf(out, "store count:          %10ld\n", store_count);
    g_string_append_printf(out, "cp count:             %10ld\n", cp_count);

    qemu_plugin_outs(out->str);

    icache_stats();
    dcache_stats();
    l2cache_stats();
}
