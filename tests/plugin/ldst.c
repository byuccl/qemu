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

// required export for it to build properly
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/******************************** definitions *********************************/
// have available the disassembled string when debugging the callbacks
// #define DEBUG_INSN_DISAS
#ifdef DEBUG_INSN_DISAS
#define LAST_INSN_BUF_SIZE 64
#endif


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

static const char* ld_prefix_lower = "ld";
static const char* str_prefix_lower = "st";

static injection_plan_t plan;

#ifdef DEBUG_INSN_DISAS
static char lastInsnStr[LAST_INSN_BUF_SIZE];
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

        // data is a pointer to a GByteArray
        // https://developer.gnome.org/glib/stable/glib-Byte-Arrays.html#GByteArray
        // const void* insn_data = qemu_plugin_insn_data(insn);
        // size_t insn_size = qemu_plugin_insn_size(insn);
        // const guint8* data_array = (guint8*)insn_data;
        // (void)data_array[insn_size-1];
        
        // we can get the disassembly of the instruction?
        char* disas_str = qemu_plugin_insn_disas(insn);
#ifdef DEBUG_INSN_DISAS
        strncpy(lastInsnStr, disas_str, LAST_INSN_BUF_SIZE);
#endif

        // I think it will always be lowercase, but not sure
        if (memcmp(disas_str, ld_prefix_lower, 2) == 0) {
            // register a callback with loading
            qemu_plugin_register_vcpu_mem_cb(insn, parse_ld,
                                        QEMU_PLUGIN_CB_NO_REGS,
                                        QEMU_PLUGIN_MEM_R,
                                        (void*)insn_vaddr);
        } else if (memcmp(disas_str, str_prefix_lower, 2) == 0) {
            // register a callback with storing
            qemu_plugin_register_vcpu_insn_exec_cb(insn, parse_st,
                                        QEMU_PLUGIN_CB_NO_REGS,
                                        (void*)insn_vaddr);
        } else {
            // register a callback for everything else to track icache uses
            qemu_plugin_register_vcpu_insn_exec_cb(
                                        insn, parse_instruction,
                                        QEMU_PLUGIN_CB_NO_REGS,
                                        (void*)insn_vaddr);
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
    // pretty sure argv[0] is NOT the name of the program, like normal
    uint64_t sleepCycles = 0;
    // have to be 64 bit so can use stroul
    uint64_t cacheRow, cacheSet, cacheBit;

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

    // TODO: optional - no arguments means only profiling
    // if there were no arguments, or not enough
    if (!sleepCycles || argc < 4) {
        return !0;
    }

    // init the icache simulation
    icache_init(32768, 4, 32, POLICY_RANDOM);
    plan.sleepCycles = sleepCycles;
    plan.cacheRow = cacheRow;
    plan.cacheSet = cacheSet;
    plan.cacheBit = cacheBit;

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
