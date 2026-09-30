TARGET = ra_psp_plugin_host
OBJS = main.o

CFLAGS = -O2 -G0 -Wall -Wextra
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LIBS = -lpspdebug -lpspdisplay -lpspge

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = RA-PSP Plugin Host

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
