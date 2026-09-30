TARGET = ra_psp_login
OBJS = main.o

CFLAGS = -O2 -G0 -Wall -Wextra -DRC_DISABLE_LUA -DRC_NO_THREADS
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)

INCDIR = ../ra-psp-core ../third_party/rcheevos/include ../third_party/rcheevos/src
LIBDIR = ../build/core ../build/rcheevos
LIBS = -lra_psp_core -lrcheevos -lm -lpsphttp -lpspnet_inet -lpspnet_apctl -lpspnet_resolver -lpsprtc -lpspdebug -lpspdisplay -lpspctrl

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = RA-PSP Login

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
