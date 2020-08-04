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
#include <fcntl.h>
#include <unistd.h>

#include <glib.h>

#include <qemu-plugin.h>

// required export for it to build properly
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/******************************** Definitions *********************************/
#define INPUT_BUF_SIZE 128
#define FUNC_NAME_SIZE 64


/**************************** function prototypes *****************************/
static void plugin_exit(qemu_plugin_id_t id, void* p);
static void read_input_file(const char* filePath);
static void on_tb_translate(qemu_plugin_id_t id, struct qemu_plugin_tb* tb);
static void print_insn_hit(unsigned int vcpu_index, void* userdata);


/********************************** globals ***********************************/
static FILE* inputFile;
static FILE* outputFile;
static GHashTable* funcMap;
static uint64_t cycleCount = 0;


/********************************* functions **********************************/

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
 *  functionName - start, end
 * Where 'start' and 'end' are 0x0000000
 */
static void read_input_file(const char* filePath) {
    // open the file
    inputFile = fopen(filePath, "r");

    // variables for reading in values
    int start, end;
    int status = 0;
    int numEntries = 0;
    char inputBuffer[INPUT_BUF_SIZE];
    char funcNameBuf[FUNC_NAME_SIZE];

    g_autoptr(GString) out = g_string_new("");

    // populate the map - read line by line
    while (fgets(inputBuffer, INPUT_BUF_SIZE, inputFile)) {
        // qemu_plugin_outs(inputBuffer);
        // parse the line
        // example - vfiprintf: 0x10aa6c, 0x10aa86
        status = sscanf(inputBuffer, "%s - 0x%x, 0x%x", funcNameBuf, &start, &end);
        if (status == 3) {
            // fprintf(outputFile, "%s goes from %#x to %#x\n", funcNameBuf, start, end);
            // put in hash table
            g_string_printf(out, "-> %s", funcNameBuf);
            g_hash_table_insert(funcMap, GINT_TO_POINTER(start), g_strdup(out->str));

            // same thing for the end address
            // g_string_printf(out, "<- %s", funcNameBuf);
            // g_hash_table_insert(funcMap, GINT_TO_POINTER(end), g_strdup(out->str));

            // track how many functions we're looking for
            numEntries += 1;
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
                    insn, print_insn_hit, QEMU_PLUGIN_CB_NO_REGS, val);
        }

        // all instructions will increment the global cycle counter
        qemu_plugin_register_vcpu_insn_exec_inline(insn,
                QEMU_PLUGIN_INLINE_ADD_U64, &cycleCount, 1);
    }
}


static void print_insn_hit(unsigned int vcpu_index, void* userdata) {
    // cast
    char* hitMsg = (char*)userdata;

    // print with cycle count
    fprintf(outputFile, "%s: %lu\n", hitMsg, cycleCount);
}
