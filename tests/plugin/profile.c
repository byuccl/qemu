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
#include <fcntl.h>
#include <unistd.h>

#include <glib.h>

#include <qemu-plugin.h>

// required export for it to build properly
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;


/**************************** function prototypes *****************************/
static void plugin_exit(qemu_plugin_id_t id, void* p);
static void read_input_file(const char* filePath);


/********************************** globals ***********************************/
static int inputFile;
static int outputFile;


/********************************* functions **********************************/

/*
 * Register the plugin.
 * This is kind of like "main".
 * Arguments:
 * inputPath - path to the input file
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                            const qemu_info_t* info,
                                            int argc, char** argv)
{
    // to be run at exit
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    g_autoptr(GString) out = g_string_new("");
    g_string_printf(out, "input file: %s\n", argv[0]);
    g_string_append_printf(out, "output file: %s\n", argv[1]);
    qemu_plugin_outs(out->str);

    read_input_file(argv[0]);

    outputFile = 0;

    return 0;
}


/*
 * Called when the plugin is to be removed (atexit)
 */
static void plugin_exit(qemu_plugin_id_t id, void* p) {

}


static void read_input_file(const char* filePath) {
    // open the file
    inputFile = open(filePath, O_RDONLY);

    // populate the map

    // close the file
    close(inputFile);
}
