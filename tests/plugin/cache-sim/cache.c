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

#include "injection.h"
#include "icache.h"
#include "dcache.h"
#include "l2cache.h"

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
static void parse_mem(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                        uint64_t vaddr, void* userdata);
static void check_insn_count(unsigned int vcpu_index, void* userdata);
static void plugin_exit(qemu_plugin_id_t id, void* p);
static void put_cbs_in_tbs(qemu_plugin_id_t id, struct qemu_plugin_tb* tb);


/********************************** globals ***********************************/
static uint64_t insn_count = 0;
static uint64_t load_count = 0;
static uint64_t store_count = 0;
static uint64_t textBegin = 0, textEnd = 0;     // begin and end addresses of .text

#ifdef DEBUG_INSN_DISAS
char lastInsnStr[LAST_INSN_BUF_SIZE];
#endif

static injection_plan_t plan;
static uint32_t faultDone = 0;


/********************************* functions **********************************/

/* 
 * Will register a callback with each instruction executed
 * Based on code in insn.c, hotpages.c, and mem.c plugins
 */
static void put_cbs_in_tbs(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    // we can disable this from the plugin init
    if (!faultDone)
    {
        // on the translation of each tb, put a callback to check cycle count
        qemu_plugin_register_vcpu_tb_exec_cb(tb, check_insn_count,
                                            QEMU_PLUGIN_CB_NO_REGS,     // TODO: RW for
                                            (void*)NULL);               //  injection?
    }

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

        if ( (insn_vaddr <= textEnd) && (insn_vaddr >= textBegin) ) {
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
    if ( (addr <= textEnd) && (addr >= textBegin) ) {
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
 * Function is called every time a tb is executed.
 * This is the stub for where fault injection will be performed.
 * TODO: this could be moved to be inside the other callbacks, to give a 
 *  more fine-grained cycle count, but at the cost of longer emulation time.
 */
static void check_insn_count(unsigned int vcpu_index, void* userdata)
{
    // see if it's time to inject the fault
    if ( (!faultDone) && (insn_count >= plan.sleepCycles) )
    {
        faultDone = 1;

        g_autoptr(GString) out = g_string_new("");
        g_string_printf(out, "Injecting fault...\n");

        // print out about the injection
        g_string_append_printf(out, "slept for %lu cycles\n", insn_count);
        g_string_append_printf(out, "injected at row %lu, set %lu, bit 0x%lX\n",
                                plan.cacheRow, plan.cacheSet, plan.cacheBit);

        qemu_plugin_outs(out->str);
    }
}


/*
 * Register the plugin.
 * This is kind of like "main".
 * Arguments:
 *  sleepCycles - the number of cycles to wait before injecting a fault
 *  cacheRow    - the row in the cache to inject fault
 *  cacheSet    - which set block
 *  cacheBit    - which bit in the block
 *  cacheName   - which cache to inject into (NYI)
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
    uint32_t numArgs = 6;
    if ((argc < 2) || (argc > 2 && argc != numArgs) )
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
            case 2:
                sleepCycles = strtoul(p, NULL, 10);
                break;
            case 3:
                cacheRow = strtoul(p, NULL, 10);
                break;
            case 4:
                cacheSet = strtoul(p, NULL, 10);
                break;
            case 5:
                cacheBit = strtoul(p, NULL, 10);
                break;
            default:
                qemu_plugin_outs("Too many input arguments to plugin!\n");
                return !0;
        }
    }

    // optional - no arguments means only profiling
    if (sleepCycles) {
        plan.sleepCycles = sleepCycles;
        plan.cacheRow = cacheRow;
        plan.cacheSet = cacheSet;
        plan.cacheBit = cacheBit;        
    } else if (argc == 0) {
        // only profiling
        faultDone = 1;
    } else {
        qemu_plugin_outs("Error parsing plugin arguments!\n");
        return !0;
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

    // print status
    g_autoptr(GString) out = g_string_new("");
    g_string_printf(out, "Initializing...\n");
    g_string_append_printf(out, "text: 0x%lX - 0x%lX\n", textBegin, textEnd);
    qemu_plugin_outs(out->str);

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

    qemu_plugin_outs(out->str);

    icache_stats();
    dcache_stats();
    l2cache_stats();
}

/*
 * Plan:
 * The plugin will keep track of the addresses in the cache,
 *  be responsible for changing values in memory (and tag bits in the future)
 * The fault injector will tell the plugin where to inject fault, and after how long.
 * Need a function to query the current number of cycles since start.
 */
