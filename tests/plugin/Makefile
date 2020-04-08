BUILD_DIR := $(CURDIR)/../..

include $(BUILD_DIR)/config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/tests/plugin)
VPATH += $(SRC_PATH)/tests/plugin/cache-sim

NAMES :=
NAMES += bb
NAMES += empty
NAMES += insn
NAMES += mem
NAMES += hotblocks
NAMES += howvec
NAMES += hotpages
NAMES += cache
CACHE_SRCS := cache.o cache-common.o icache.o dcache.o l2cache.o arm-disas.o sockets.o

SONAMES := $(addsuffix .so,$(addprefix lib,$(NAMES)))

QEMU_CFLAGS += -fPIC
QEMU_CFLAGS += -I$(SRC_PATH)/include/qemu

all: $(SONAMES)

libcache.so: $(CACHE_SRCS)
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

lib%.so: %.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

clean:
	rm -f *.o *.so *.d
	rm -Rf .libs

.PHONY: all clean
