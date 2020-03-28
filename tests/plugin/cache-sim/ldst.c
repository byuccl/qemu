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
#include "injection.h"
#include "arm-disas.h"

// required export for it to build properly
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/******************************** definitions *********************************/
// have available the disassembled string when debugging the callbacks
// #define DEBUG_INSN_DISAS
#ifdef DEBUG_INSN_DISAS
#define LAST_INSN_BUF_SIZE 64
#endif

// useful macros for registering a specific type of callback
#define SET_LOAD_CB(insn, userp) \
                    qemu_plugin_register_vcpu_mem_cb(insn, parse_ld,    \
                                    QEMU_PLUGIN_CB_R_REGS,              \
                                    QEMU_PLUGIN_MEM_R,                  \
                                    (void*)userp)
#define SET_STORE_CB(insn, userp) \
                    qemu_plugin_register_vcpu_insn_exec_cb(insn, parse_st,  \
                                    QEMU_PLUGIN_CB_R_REGS,                  \
                                    (void*)userp)


/**************************** function prototypes *****************************/
static void parse_instruction(unsigned int vcpu_index, void* userdata);
static void parse_ld(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                        uint64_t vaddr, void* userdata);
static void parse_st(unsigned int vcpu_index, void* userdata);
static void plugin_exit(qemu_plugin_id_t id, void* p);
static void put_cbs_in_tbs(qemu_plugin_id_t id, struct qemu_plugin_tb* tb);

/********************************** globals ***********************************/
static uint64_t insn_count = 0;
static uint64_t load_count = 0;
static uint64_t store_count = 0;

#if defined(USE_STRING_COMPARE) || defined(DEBUG_INSN_DISAS)
static const char* ld_prefix_lower = "ld";
static const char* str_prefix_lower = "st";
#endif

static injection_plan_t plan;

#ifdef DEBUG_INSN_DISAS
char lastInsnStr[LAST_INSN_BUF_SIZE];
#endif


/********************************* functions **********************************/

/* 
 * Will register a callback with each instruction executed
 * Based on code in insn.c and mem.c plugins
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

        // data is a pointer to a GByteArray
        // https://developer.gnome.org/glib/stable/glib-Byte-Arrays.html#GByteArray
        const void* insn_data = qemu_plugin_insn_data(insn);
        size_t insn_size = qemu_plugin_insn_size(insn);
        if (insn_size != 4) {
            // error, we expected only ARM 32-bit instruction, no aarch64 or thumb
        }

        const guint8* data_array = (guint8*)insn_data;
        // do we need to worry about endianness?
        // no, the encoding matches the ARM reference
        // however, this won't work if it's a T-32 instruction
        //  have to get the last 2 bytes first, then the first 2
        // TODO: look at this - https://developer.gnome.org/glib/stable/glib-Byte-Order-Macros.html
        uint32_t insn_bits = 0;
        int i;
        for (i = insn_size - 1; i >= 0; i--) {
            insn_bits |= (data_array[i] << (i * 8));
        }

        // we're going to gather information about the instruction
        // TODO: this will need to go in some kind of table, and pass the
        //  base address in to the callbacks
        // TODO: would this be able to go in some kind of table where the
        //  encoded bits are the key? then we would only have to translate
        //  each instruction encoding once
        // https://blog.sensecodons.com/2012/01/glib-ghashtable-and-gdirecthash.html
        // https://developer.gnome.org/glib/stable/glib-Hash-Tables.html
        insn_op_t insn_op_data;

        // decode the instruction data
        if (INSN_IS_LOAD_STORE(insn_bits)) {
            load_store_e type = decode_load_store(&insn_op_data, insn_bits);
            if (type) {
                if (type < LD_TYPE_BASE) {
                    // store
                    SET_STORE_CB(insn, insn_vaddr);
                } else {
                    // load
                    SET_LOAD_CB(insn, insn_vaddr);
                }
                continue;
            }
        }

        // check for block ld/st
        if (INSN_IS_BLOCK_LOAD_STORE(insn_bits)) {
            block_load_store_e type = decode_block_load_store(&insn_op_data, insn_bits);
            if (type) {
                if (type < LD_BLK_TYPE_BASE) {
                    // store
                    SET_STORE_CB(insn, insn_vaddr);
                } else {
                    // load
                    SET_LOAD_CB(insn, insn_vaddr);
                }
                continue;
            }
        }

        // check for extra ld/st
        int is_extra = INSN_IS_EXTRA_LOAD_STORE(insn_bits);
        // and also synchronization primitives
        if (is_extra == MISC_IS_SYNC_PRIMITIVE) {
            sync_load_store_e type = decode_sync_load_store(&insn_op_data, insn_bits);
            if (type >= SWAP_WORD) {
                // it's both!
                SET_STORE_CB(insn, insn_vaddr);
                SET_LOAD_CB(insn, insn_vaddr);
            } else if (type < LD_SYNC_TYPE_BASE) {
                // store
                SET_STORE_CB(insn, insn_vaddr);
            } else {
                // load
                SET_LOAD_CB(insn, insn_vaddr);
            }
            continue;
        }
        else if (is_extra) {
            extra_load_store_e type = decode_extra_load_store(&insn_op_data, insn_bits);
            if (type) {
                if (type < LD_EXTRA_TYPE_BASE) {
                    // store
                    SET_STORE_CB(insn, insn_vaddr);
                } else {
                    // load
                    SET_LOAD_CB(insn, insn_vaddr);
                }
                continue;
            }
        }

        // check for coprocessor ld/st
        cp_load_store_e coproc_ldst = \
                INSN_IS_COPROC_LOAD_STORE(&insn_op_data, insn_bits);
        if (coproc_ldst) {
            if (coproc_ldst < LD_CP_TYPE_BASE) {
                // store
                SET_STORE_CB(insn, insn_vaddr);
            } else {
                // load
                SET_LOAD_CB(insn, insn_vaddr);
            }
        }

#ifdef DEBUG_INSN_DISAS
        // check for any stragglers
        if ((memcmp(disas_str, ld_prefix_lower, 2) == 0) ||
            (memcmp(disas_str, str_prefix_lower, 2) == 0))
        {
            g_autofree gchar *out;
            out = g_strdup_printf("insn: %s\n", disas_str);
            qemu_plugin_outs(out);
        }
#endif

        // register a callback for everything else to track icache uses
        qemu_plugin_register_vcpu_insn_exec_cb(
                                    insn, parse_instruction,
                                    QEMU_PLUGIN_CB_NO_REGS,
                                    (void*)insn_vaddr);
    }
}


/*
 * Current numbers for test (MxM):
 * 
 * insn count: 115390281
 * load count:  78841153
 * store count:  2862292
 *
 * A5.5 - Table A5-21 - page 214
 * 0x105ae4: 0x2300e9d1 - ldrd r2, r3, [r1]
 *  actually e9d1 2300, because it's in 32-bit Thumb mode for strlen()
 * 1110 1001 1101 0001 0010 0011 0000 0000
 * see A6.3.6, also A6.3, table A6-9 on page 230
 * So how do we determine if an instruction is in Thumb mode, especially
 *  when it still is a 32-bit instruction?
 */


/*
 * Function is called every time a non-memory instruction is executed.
 */
static void parse_instruction(unsigned int vcpu_index, void* userdata)
{
    insn_count += 1;
    icache_load((uint64_t)userdata);
}


/*
 * Function is called every time a load instruction is executed.
 */
static void parse_ld(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                        uint64_t vaddr, void* userdata)
{
    struct qemu_plugin_hwaddr* hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    (void)hwaddr;
    // if hwaddr->is_io == true, then use hwaddr->v.io for section and offset
    //  (see also include/exec/memory.h for definition of MemoryRegionSection)
    // if hwaddr->is_io == false, then use hwaddr->v.ram.hostaddr
    // include/qemu/plugin-memory.h

    load_count += 1;
    insn_count += 1;
    icache_load((uint64_t)userdata);
}


/*
 * Function is called every time a store instruction is executed.
 */
static void parse_st(unsigned int vcpu_index, void* userdata)
{
    store_count += 1;
    insn_count += 1;
    icache_load((uint64_t)userdata);
}


/*
 * Register the plugin.
 * This is kind of like "main".
 * Arguments:
 *  sleepCycles - the number of cycles to wait before injecting a fault
 *  cacheRow    - the row in the cache to inject fault
 *  cacheSet    - which set block
 *  cacheBit    - which bit in the block
 *  doTag       - if the bit should be in the tag bits instead of data (NYI)
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                            const qemu_info_t* info,
                                            int argc, char** argv)
{
    // parse arguments to the plugin
    // argv[0] is NOT the name of the program, like normal
    uint64_t sleepCycles = 0;
    // have to be 64 bit so can use stroul
    uint64_t cacheRow, cacheSet, cacheBit;

    // allow all args or none
    uint32_t numArgs = 4;
    if (argc > 0 && argc != numArgs) {
        qemu_plugin_outs("Wrong number of arguments to plugin!\n");
        return !0;
    }

    // based on example in howvec.c
    for (int i = 0; i < argc; i++) {
        char* p = argv[i];
        switch (i) {
            case 0:
                sleepCycles = strtoul(p, NULL, 10);
                break;
            case 1:
                cacheRow = strtoul(p, NULL, 10);
                break;
            case 2:
                cacheSet = strtoul(p, NULL, 10);
                break;
            case 3:
                cacheBit = strtoul(p, NULL, 10);
                break;
            default:
                qemu_plugin_outs("Too many input arguments to plugin!\n");
                return !0;
        }
    }

    // optional - no arguments means only profiling
    if (sleepCycles) {
        // init the icache simulation
        icache_init(32768, 4, 32, POLICY_RANDOM);
        plan.sleepCycles = sleepCycles;
        plan.cacheRow = cacheRow;
        plan.cacheSet = cacheSet;
        plan.cacheBit = cacheBit;        
    } else {
        // only profiling - NYI
    }

    // `info` argument has information about qemu system state
    // see qemu_info_t in include/qemu/qemu-plugin.h for more details

    // register the functions in this file
    qemu_plugin_register_vcpu_tb_trans_cb(id, put_cbs_in_tbs);

    // to be run at exit
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}


/*
 * Called when the plugin is to be removed (atexit)
 */
static void plugin_exit(qemu_plugin_id_t id, void* p) {
    // based on example in mem.c
    g_autoptr(GString) out = g_string_new("");
    
    g_string_printf(out, "insn count: %ld\n", insn_count);
    g_string_append_printf(out, "load count: %ld\n", load_count);
    g_string_append_printf(out, "store count: %ld\n", store_count);

    // print out about the injection
    g_string_append_printf(out, "slept for %lu cycles\n", plan.sleepCycles);
    g_string_append_printf(out, "injected at row %lu, set %lu, bit 0x%lX\n",
                            plan.cacheRow, plan.cacheSet, plan.cacheBit);

    qemu_plugin_outs(out->str);

    icache_stats();
}

/*
 * Plan:
 * The plugin will keep track of the addresses in the cache,
 *  be responsible for changing values in memory (and tag bits in the future)
 * The fault injector will tell the plugin where to inject fault, and after how long.
 * Need a function to query the current number of cycles since start.
 */
