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
#include <time.h>

#include <glib.h>

#include <qemu-plugin.h>

#include "injection.h"
#include "../cache-sim/icache.h"
#include "../cache-sim/dcache.h"
#include "../cache-sim/l2cache.h"
#include "../cache-sim/arm-disas.h"
#include "sockets.h"

// required export for it to build properly
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/******************************** definitions *********************************/
// have available the disassembled string when debugging the callbacks
#define DEBUG_INSN_DISAS
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
static void check_insn_count(void);
static void plugin_exit(qemu_plugin_id_t id, void* p);
static void put_cbs_in_tbs(qemu_plugin_id_t id, struct qemu_plugin_tb* tb);
static void receive_injection_info(void);
// helpers
void get_socket_args(void);

// hack - https://stackoverflow.com/a/61977875/12940429
void *qemu_get_cpu(int index);
static uint32_t get_cpu_register(unsigned int cpu_index, unsigned int reg);


/********************************** globals ***********************************/
static uint64_t insn_count = 0;
static uint64_t load_count = 0;
static uint64_t store_count = 0;
static uint64_t cp_count = 0;
static uint64_t tb_count = 0;
static uint64_t uniq_insn_count = 0;
static uint64_t textBegin = 0, textEnd = 0;     // begin and end addresses of .text
static uint64_t mainBegin = 0;
static uint64_t otherCycle = 0;
static uint64_t otherCycleSkip = 0;
static uint64_t otherCycleCount = 0;

#ifdef DEBUG_INSN_DISAS
char lastInsnStr[LAST_INSN_BUF_SIZE];
#endif

static injection_plan_t plan;
static uint32_t faultDone = 0;
static uint64_t doInject = 0;

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
 * sleepCycles - the number of cycles to wait before injecting a fault
 */
static void put_cbs_in_tbs(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    // do stuff the first time
    if (!tb_count) {
        // look for data from the socket
        if (doInject) {
            // get sleep cycles only here
            char* argStr;
            argStr = sockets_recv();
            plan.sleepCycles = strtoul(argStr, NULL, 10);
            free(argStr);
            g_autoptr(GString) out = g_string_new("");
            // to get it to print
            g_string_printf(out, "INFO: Sleeping for %ld cycles\n", plan.sleepCycles);
            qemu_plugin_outs(out->str);
        } else {
            // ask for what the address of main() is so we can tell how many
            //  cycles it takes to get there
            // We also support requesting other specific address cycle counts - unstable
            char* argStr;
            argStr = sockets_recv();
            mainBegin = strtoul(argStr, NULL, 10);
            free(argStr);
            g_autoptr(GString) out = g_string_new("");
            // special value of 0 means it's not actually main
            if (mainBegin == 0) {
                // get the next value instead
                argStr = sockets_recv();
                otherCycle = strtoul(argStr, NULL, 10);
                free(argStr);
                // then how many times to pass it (like GDB breakpoint)
                argStr = sockets_recv();
                otherCycleSkip = strtoul(argStr, NULL, 10);
                free(argStr);
                g_string_printf(out, "INFO: otherAddr specified: 0x%lX; hit %lu times\n", otherCycle, otherCycleSkip);
            } else {
                // to get it to print
                g_string_printf(out, "INFO: main() starts at 0x%lX\n", mainBegin);
            }
            qemu_plugin_outs(out->str);
        }
    }
    // number of unique tb's
    tb_count += 1;

    // get the number of instructions in this tb
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    // iterate over each instruction and register a callback with it
    for (i = 0; i < n; i++) {
        uniq_insn_count += 1;
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
    uint64_t vaddr = (uint64_t)userdata;
    insn_count += 1;
    icache_load(vaddr);
    check_insn_count();

    // see if we've hit main
    if (!doInject) {
        if (mainBegin == 0) {
            // dangerous match
            if (vaddr == otherCycle) {
                // we've hit it another time
                otherCycleCount += 1;
                if (otherCycleCount == otherCycleSkip) {
                    g_autoptr(GString) out = g_string_new("");
                    g_string_printf(out, "0x%08lX", insn_count);
                    sockets_send(out->str, out->len);
                    // debug print
                    g_string_printf(out, "INFO: reached 0x%lX at %lu cycles\n", otherCycle, insn_count);
                    qemu_plugin_outs(out->str);
                }
            }
        } else if (vaddr >= mainBegin) {
            // conservative match
            g_autoptr(GString) out = g_string_new("");
            g_string_printf(out, "0x%08lX", insn_count);
            sockets_send(out->str, out->len);
            // reset mainBegin to be max int
            mainBegin = UINT64_MAX;
            // debug print
            g_string_printf(out, "INFO: reached main() at %lu cycles\n", insn_count);
            qemu_plugin_outs(out->str);
        }
    }
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

    // invalidate a specific cache row and block
    dcache_invalidate_block(readSet, readWay);

    cp_count += 1;
}

/*
 * There is a single instruction that invalides the entire icache
￼ * https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L224
￼ *    mcr	p15, 0, r0, c7, c5, 0
￼ * Executes the "ICIALLU" pseudo-instruction
￼ * https://developer.arm.com/docs/ddi0595/h/aarch32-system-instructions/iciallu
￼ * Don't even need to read the register.
￼ */
static void icache_inst(unsigned int vcpu_index, void* userdata)
{
    icache_invalidate_all();
    cp_count += 1;
}


/*
 * TODO: L2 cache invalidate
 * https://developer.arm.com/documentation/ddi0246/f/
 * CoreLink Level 2 Cache Controller L2C-310 Technical Reference Manual
 * Section 3.1.1 describes the initialization sequence.
 * Part of this sequence is to write 0xFFFF to offset 0x77C, so this could be caught
 *  by the plugin if it knew the base address of the L2 cache controller.
 * For an example, see
 * https://github.com/Xilinx/embeddedsw/blob/a60c084a0862559e2fa58fd4c82b0fe39b923e33/lib/bsp/standalone/src/arm/cortexa9/gcc/boot.S#L348
 */


/*
 * Function is called every time a tb is executed.
 * This is the stub for where fault injection will be performed.
 * TODO: this could be moved to be inside the other callbacks, to give a 
 *  more fine-grained cycle count, but at the cost of longer emulation time.
 */
static void check_insn_count(void)
{
    // see if it's time to inject the fault
    if ( (!faultDone) && (insn_count >= plan.sleepCycles) )
    {
        faultDone = 1;

        g_autoptr(GString) out = g_string_new("");
        // to get it to print
        g_string_printf(out, "INFO: Injecting fault...\n");
        qemu_plugin_outs(out->str);

        receive_injection_info();
        // print out about the injection
        g_string_printf(out, "INFO: injecting at row %lu, set %lu, word 0x%lX\n",
                                plan.cacheRow, plan.cacheSet, plan.cacheWord);
        qemu_plugin_outs(out->str);

        // which cache?
        arch_word_t addr;
        switch (plan.cacheName) {
            case ICACHE:
                addr = icache_get_addr(plan.cacheRow, plan.cacheSet);
                break;
            case DCACHE:
                addr = dcache_get_addr(plan.cacheRow, plan.cacheSet);
                break;
            case L2CACHE:
                addr = l2cache_get_addr(plan.cacheRow, plan.cacheSet);
                break;
        }
        // what is the target word?
        // although it's byte addressable, injector operates on words
        uint32_t byteNum = plan.cacheWord * sizeof(arch_word_t);
        addr += byteNum;

        // send how many cycles it actually was
        g_string_printf(out, "0x%08lX", insn_count);
        sockets_send(out->str, out->len);
        // send the data
        g_string_printf(out, "0x%08X", addr);
        sockets_send(out->str, out->len);

        // TODO: socket return value for success or failure
    }
}


/****************************** argument helpers ******************************/
void get_socket_args(void)
{
    // where in the cache - row and set
    char* argStr = sockets_recv();
    plan.cacheRow = strtoul(argStr, NULL, 10);
    free(argStr);
    argStr = sockets_recv();
    plan.cacheSet = strtoul(argStr, NULL, 10);
    free(argStr);

    char* cacheName = sockets_recv();
    // decode the cache name
    if (memcmp(cacheName, "icache", 6) == 0) {
        plan.cacheName = ICACHE;
    } else if (memcmp(cacheName, "dcache", 6) == 0) {
        plan.cacheName = DCACHE;
    } else if (memcmp(cacheName, "l2cache", 7) == 0) {
        plan.cacheName = L2CACHE;
    } else {
        qemu_plugin_outs("ERROR: Invalid cache name!\n");
        // return !0;
    }
    free(cacheName);
}

/*
 *  Arguments (from the socket):
 *  cacheRow    - the row in the cache to inject fault
 *  cacheSet    - which set block
 *  cacheBit    - which bit in the block
 *                possible values: 0 -> ((blockSize * 8) - 1)
 *  cacheName   - which cache to inject into
 *  doTag       - if the bit should be in the tag bits instead of data (NYI)
 */
static void receive_injection_info(void)
{
    qemu_plugin_outs("INFO: Waiting for socket args\n");

    // where in the cache
    get_socket_args();
    // verify those are valid parameters
    int invalid = 1;
    switch (plan.cacheName) {
        case ICACHE:
            invalid = icache_validate_injection(&plan);
            break;
        case DCACHE:
            invalid = dcache_validate_injection(&plan);
            break;
        case L2CACHE:
            invalid = l2cache_validate_injection(&plan);
            break;
        default:
            invalid = 1;
    }
    if (invalid) {
        qemu_plugin_outs("ERROR: Invalid injection parameters!\n");
        // return !0;
    }

    // see if the line is valid
    uint8_t valid = 0;
    switch (plan.cacheName) {
        case ICACHE:
            valid = icache_block_valid(plan.cacheRow, plan.cacheSet);
            break;
        case DCACHE:
            valid = dcache_block_valid(plan.cacheRow, plan.cacheSet);
            break;
        case L2CACHE:
            valid = l2cache_block_valid(plan.cacheRow, plan.cacheSet);
            break;
        default:
            valid = 0;
            break;
    }

    // send valid-ness back to the supervisor
    g_autoptr(GString) out = g_string_new("");
    g_string_printf(out, "%hhu\n", valid);    // unsigned char
    sockets_send(out->str, out->len);
    // some discussion about waiting for a valid block, but that is not realistic
    //   of radiation tests, even if it takes longer

    // then get the block
    char* argStr = sockets_recv();
    plan.cacheWord = strtoul(argStr, NULL, 10);
    free(argStr);

    // NOTE: no checking is done on the cycle count: if that doesn't happen
    //  before the program exits, that is the user's problem
}


/*
 * Register the plugin.
 * This is kind of like "main".
 * Arguments:
 * textBegin - the address that the .text section starts at
 * textEnd   - the address that the .text section ends at
 * portNum   - port number of socket
 * hostname  - IPV4 address of socket
 * doInject  - whether or not to inject a fault
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                            const qemu_info_t* info,
                                            int argc, char** argv)
{
    // parse arguments to the plugin
    // argv[0] is NOT the name of the program, like normal
    uint16_t portNum;
    char* hostname;

    // only allow ARM target
    if (strcmp(info->target_name, "arm") != 0) {
        qemu_plugin_outs("ERROR: Architecture not supported!\n");
        return !0;
    }

    // allow all args or none
    uint32_t minArgs = 2;
    uint32_t numArgs = 5;
    if ((argc < minArgs) || (argc > minArgs && argc != numArgs) )
    {
        qemu_plugin_outs("ERROR: Wrong number of arguments to plugin!\n");
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
                // we're using IPV4
                // TODO: don't need socket if not injecting
                portNum = (uint16_t) strtoul(p, NULL, 10);
                break;
            case 3:
                hostname = p;
                break;
            case 4:
                doInject = strtoul(p, NULL, 10);
                break;
            default:
                qemu_plugin_outs("ERROR: Too many input arguments to plugin!\n");
                return !0;
        }
    }

    // set up the socket for communication
    if (sockets_init(portNum, hostname)) {
        qemu_plugin_outs("ERROR: setting up socket!\n");
        return !0;
    }

    if (!doInject) {
        // skip inserting the callbacks if not injecting
        faultDone = 1;
    }

    // init the cache simulation
    // TODO: should we make this parametrized by socket as well?
    icache_init(ICACHE_SIZE_BYTES, ICACHE_ASSOCIATIVITY,
                ICACHE_BLOCK_SIZE, ICACHE_REPLACE_POLICY,
                ICACHE_ALLOC_POLICY);
    dcache_init(DCACHE_SIZE_BYTES, DCACHE_ASSOCIATIVITY,
                DCACHE_BLOCK_SIZE, DCACHE_REPLACE_POLICY,
                DCACHE_ALLOC_POLICY);
    l2cache_init(L2CACHE_SIZE_BYTES, L2CACHE_ASSOCIATIVITY,
                L2CACHE_BLOCK_SIZE, L2CACHE_REPLACE_POLICY,
                L2CACHE_ALLOC_POLICY);

    // register the functions in this file
    qemu_plugin_register_vcpu_tb_trans_cb(id, put_cbs_in_tbs);

    // to be run at exit
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    // print status
    g_autoptr(GString) out = g_string_new("");
    g_string_printf(out, "INFO: do inject? %ld\n", doInject);
    g_string_append_printf(out, "INFO: text: 0x%lX - 0x%lX\n", textBegin, textEnd);
    g_string_append_printf(out, "INFO: target: %s\n", info->target_name);
    qemu_plugin_outs(out->str);

    return 0;
}


/*
 * Called when the plugin is to be removed (atexit)
 */
static void plugin_exit(qemu_plugin_id_t id, void* p) {
    // based on example in mem.c
    g_autoptr(GString) out = g_string_new("");

    if (plan.sleepCycles == 0) {
        g_string_printf(out,        "INFO: insn count:           %10ld\n", insn_count);
        g_string_append_printf(out, "INFO: load count:           %10ld\n", load_count);
        g_string_append_printf(out, "INFO: store count:          %10ld\n", store_count);
        g_string_append_printf(out, "INFO: cp count:             %10ld\n", cp_count);
        g_string_append_printf(out, "INFO: tb count:             %10ld\n", tb_count);
        g_string_append_printf(out, "INFO: unique insns:         %10ld\n", uniq_insn_count);
        qemu_plugin_outs(out->str);

        // if there was no injection, report the number of cycles
        g_string_printf(out, "0x%08lX\n", insn_count);
        sockets_send(out->str, out->len);
    }

    sockets_exit();
}

/*
 * Plan:
 * The plugin will keep track of the addresses in the cache,
 *  be responsible for changing values in memory (and tag bits in the future)
 * The fault injector will tell the plugin where to inject fault, and after how long.
 * Need a function to query the current number of cycles since start.
 */
