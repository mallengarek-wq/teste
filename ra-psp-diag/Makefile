TARGET = ra_psp_diag
OBJS = main.o

INCDIR =
CFLAGS = -O2 -G0 -Wall -Wextra
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LIBS =
LDFLAGS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = RA-PSP Diagnostic v0.0.2

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
