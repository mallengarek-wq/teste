TARGET = ra_psp_ingame
OBJS = main.o

CFLAGS = -O2 -G0 -Wall -Wextra -DRC_DISABLE_LUA -DRC_NO_THREADS
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LDFLAGS = -nostartfiles

INCDIR = ../ra-psp-core ../third_party/rcheevos/include ../third_party/rcheevos/src
LIBDIR = ../build/core ../build/rcheevos
LIBS = -lra_psp_core -lrcheevos -lm -lpsphttp -lpspnet_inet -lpspnet_apctl -lpspnet_resolver -lpsprtc -lpspdisplay -lpspctrl

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
