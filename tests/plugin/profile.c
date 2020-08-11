/*
 * Qemu plugin to profile execution.
 * Simulates processor caches.
 *
 * Initial idea:
 - make a new plugin
 - at start, take variable length list of addresses to watch for
 - when these instruction addresses are hit, send the address and cycle count back
 - communication on socket, in JSON format
 - Python driver, read ELF file for function addresses
 *
 * New idea:
 - There is a file with the input information; pass filename as arg
 - Write profile to output file
 - This way, no sockets are needed
 - Still have a Python driver to generate input and parse output
 */

/********************************** includes **********************************/
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib.h>

#include <qemu-plugin.h>

// required export for it to build properly
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/******************************** Definitions *********************************/
#define INPUT_BUF_SIZE 128
#define FUNC_NAME_SIZE 64
// hack
#define SIZE_OF_CPUState 33480
#define SIZE_OF_CPUNegativeOffsetState 3632
// thanks - https://stackoverflow.com/a/53884709/12940429
#define CPU_STRUCT_OFFSET (SIZE_OF_CPUState + SIZE_OF_CPUNegativeOffsetState + 8)
// investigation with GDB shows that the above calculation is off by 8 bytes

// check FreeRTOSConfig.h, but default is 16
#define MAX_TASK_NAME_LEN 16


/**************************** function prototypes *****************************/
static void plugin_exit(qemu_plugin_id_t id, void* p);
static void read_input_file(const char* filePath);
static void on_tb_translate(qemu_plugin_id_t id, struct qemu_plugin_tb* tb);
static void print_insn_hit(unsigned int vcpu_index, void* userdata);
static void print_context_switch(unsigned int vcpu_index, void* userdata);
// hack - https://stackoverflow.com/a/61977875/12940429
void *qemu_get_cpu(int index);
static uint32_t get_cpu_register(unsigned int cpu_index, unsigned int reg);
extern void cpu_physical_memory_rw(uint64_t addr, uint8_t *buf,
                                    uint64_t len, int is_write);


/********************************** globals ***********************************/
static FILE* inputFile;
static FILE* outputFile;
static GHashTable* funcMap;
static uint64_t cycleCount = 0;
static uint64_t curTCBaddr = 0;
static char taskNameBuf[MAX_TASK_NAME_LEN];


/********************************* functions **********************************/

static uint32_t get_cpu_register(unsigned int cpu_index, unsigned int reg) {
    uint8_t* cpu = qemu_get_cpu(cpu_index);
    return *(uint32_t*)( cpu + CPU_STRUCT_OFFSET + (reg * 4) );
}


/*
 * Register the plugin.
 * This is kind of like "main".
 * Arguments:
 * inputPath - path to the input file
 * outputPath - path to the output file
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                            const qemu_info_t* info,
                                            int argc, char** argv)
{
    // to be run at exit
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    if (argc != 2) {
        qemu_plugin_outs("Error, invalid number of arguments!\n");
        return !0;
    }

    // verbose logging
    g_autoptr(GString) out = g_string_new("");
    g_string_printf(out, "input file: %s\n", argv[0]);
    g_string_append_printf(out, "output file: %s\n", argv[1]);
    qemu_plugin_outs(out->str);

    // open output log file
    outputFile = fopen(argv[1], "w");

    // init the maps
    // https://blog.sensecodons.com/2012/01/glib-ghashtable-and-gdirecthash.html
    funcMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

    // read in map of function start addresses
    read_input_file(argv[0]);

    // register the callback functions
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_translate);

    return 0;
}


/*
 * Called when the plugin is to be removed (atexit)
 */
static void plugin_exit(qemu_plugin_id_t id, void* p) {
    fclose(outputFile);
    g_hash_table_destroy(funcMap);
}


/*
 * Opens the input file and reads the data into the map.
 * Assumes lines are of the format
 *  functionName - start; end
 * Where 'start' and 'end' are decimal integers
 *
 * Example:
 * main - 1049780; 1049940
 * abort - 1050100
 * Xil_L1ICacheEnable - 1056348; 1056356, 1056376
 */
static void read_input_file(const char* filePath) {
    // open the file
    inputFile = fopen(filePath, "r");

    // variables for reading in values
    uint64_t start, end;
    size_t curLen;
    int numRead, numRead2 = 0;
    int status = 0;
    int numEntries = 0;
    char inputBuffer[INPUT_BUF_SIZE];
    char funcNameBuf[FUNC_NAME_SIZE];

    g_autoptr(GString) out = g_string_new("");

    // populate the map - read line by line
    while (fgets(inputBuffer, INPUT_BUF_SIZE, inputFile)) {
        // qemu_plugin_outs(inputBuffer);
        // parse the line
        status = sscanf(inputBuffer, "%s - %lu%n", funcNameBuf, &start, &numRead);
        if (status == 2) {
            // track how many functions we're looking for
            numEntries += 1;
            curLen = strlen(inputBuffer);
            // reset
            numRead2 = 0;

            // special case
            if (strcmp(funcNameBuf, "pxCurrentTCB") == 0) {
                curTCBaddr = start;
                numEntries -= 1;
                continue;
            }

            // fprintf(outputFile, "%s starts at %#lx\n", funcNameBuf, start);
            // g_string_printf(out, "\nrest of string: %s", inputBuffer+numRead);
            // qemu_plugin_outs(out->str);

            // see if (and how many) end points specified
            if (numRead+1 == curLen) {
                // put in hash table, with notation
                g_string_printf(out, "-*> %s", funcNameBuf);
                g_hash_table_insert(funcMap, GINT_TO_POINTER(start), g_strdup(out->str));
                // qemu_plugin_outs(out->str);
            } else {
                // we can figure out the rest of the data
                g_string_printf(out, "-> %s", funcNameBuf);
                g_hash_table_insert(funcMap, GINT_TO_POINTER(start), g_strdup(out->str));
                // qemu_plugin_outs(out->str);

                do {
                    // increment char pointer
                    numRead += numRead2;
                    numRead += 2;
                    if ( (numRead > INPUT_BUF_SIZE) || (numRead > curLen) ) {
                        break;
                    }

                    // g_string_printf(out, "%s", inputBuffer+numRead);
                    // qemu_plugin_outs(out->str);
                    // read next token
                    status = sscanf(inputBuffer+numRead, "%lu%n", &end, &numRead2);
                    // add to map
                    g_string_printf(out, "<- %s", funcNameBuf);
                    g_hash_table_insert(funcMap, GINT_TO_POINTER(end), g_strdup(out->str));
                    // qemu_plugin_outs(out->str);
                } while (status == 1);
            }
        }
    }

    // close the file
    fclose(inputFile);
}


static void on_tb_translate(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    // get the number of instructions in this tb
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    // iterate over each instruction and see if it needs a callback
    for (i = 0; i < n; i++) {
        // get the handle for the instruction
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);

        // what is the address of the instruction?
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);

        // look up the address
        gpointer val = g_hash_table_lookup(funcMap, GINT_TO_POINTER(insn_vaddr));
        if (val != NULL) {
            // register a printing callback
            qemu_plugin_register_vcpu_insn_exec_cb(
                    insn, print_insn_hit, QEMU_PLUGIN_CB_R_REGS, val);
            // special case
            if (memcmp(val, "<- vTaskSwitchContext", 19) == 0) {
                qemu_plugin_register_vcpu_insn_exec_cb(
                        insn, print_context_switch, QEMU_PLUGIN_CB_NO_REGS, NULL);
            }
        }

        // all instructions will increment the global cycle counter
        qemu_plugin_register_vcpu_insn_exec_inline(insn,
                QEMU_PLUGIN_INLINE_ADD_U64, &cycleCount, 1);
    }
}


static void print_insn_hit(unsigned int vcpu_index, void* userdata) {
    // cast
    char* hitMsg = (char*)userdata;

    // get the calling function
    uint32_t read_lr = get_cpu_register(vcpu_index, 1);

    // print with cycle count and return address
    // probably will have to use addr2line to figure out which function that came from
    fprintf(outputFile, "%s: %lu, %#x\n", hitMsg, cycleCount, read_lr);

    return;
}


static void print_context_switch(unsigned int vcpu_index, void* userdata) {
    // read the value of pxCurrentTCB
    uint32_t pxCurrentTCBval;
    cpu_physical_memory_rw(curTCBaddr, (uint8_t*) &pxCurrentTCBval, sizeof(pxCurrentTCBval), 0);

    // compute offset - ran GDB to figure it out
    uint32_t pxCurrentTCBname = pxCurrentTCBval + 0x34;

    // read the name from that address
    cpu_physical_memory_rw(pxCurrentTCBname, (uint8_t*) taskNameBuf, MAX_TASK_NAME_LEN, 0);

    // print result to log file
    fprintf(outputFile, "~ switch to %s\n", taskNameBuf);

    /*
     * Another way to uniquely identify tasks is using uxTCBNumber, in case
     *  there are duplicate task names. Though duplicate task names would mess
     *  with xTaskGetHandle() working, so it might not be a problem.
     * If you want to use uxTCBNumber, it's (pxCurrentTCB + 0x34 +
     *  sizeof(pxCurrentTCB->pcTaskName)).
     */
}


/*
 * Related work:
 *
 * https://github.com/guillon/run-qemu-profile
 * out of date (2014)
 * 
 * Peter Maydell says it's not built in, but a plugin could do it
 * https://stackoverflow.com/questions/58766571/how-to-count-the-number-of-guest-instructions-qemu-executed-from-the-beginning-t
 *
 * My own question shows up on SO
 * https://stackoverflow.com/questions/60821772/qemu-plugin-functions-how-to-access-guest-memory-and-registers
 *
 * Interrupt-based method for QEMU
 * http://torokerneleng.blogspot.com/2019/07/qprofiler-profiler-for-guests-in-qemukvm.html
 */
