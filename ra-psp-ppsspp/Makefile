TARGET = ra_psp_ppsspp
OBJS = main.o ra_network.o ra_memory_psp.o

INCDIR = ../third_party/rcheevos/include ../third_party/rcheevos/src
LIBDIR = ../build/rcheevos

CFLAGS = -O2 -G0 -Wall -Wextra -DRC_NO_THREADS -DRC_DISABLE_LUA
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LDFLAGS = -nostartfiles

# Keep only non-default libraries here. PSPSDK build.mak appends the core SDK
# libraries in the correct order required by psp-fixup-imports.
LIBS = -lrcheevos -lpsphttp -lpspssl -lm

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
