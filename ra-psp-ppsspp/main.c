#include <pspkernel.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>

PSP_MODULE_INFO("RA-PSP PPSSPP Probe", PSP_MODULE_USER, 1, 3);
PSP_NO_CREATE_MAIN_THREAD();

#define LOG_PATH "ms0:/PSP/PLUGINS/RA-PSP/ra_psp_ppsspp.log"
#define COMBO (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT)

static void write_line(const char *text, int len) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, text, len);
        sceIoWrite(fd, "\r\n", 2);
        sceIoClose(fd);
    }
}

static int worker(SceSize args, void *argp) {
    SceCtrlData pad;
    unsigned int prev_buttons = 0;
    int combo_latched = 0;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.3: worker started", 27);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    for (;;) {
        pad.Buttons = 0;
        pad.Lx = 128;
        pad.Ly = 128;
        sceCtrlPeekBufferPositive(&pad, 1);

        if ((pad.Buttons & COMBO) == COMBO) {
            if (!combo_latched) {
                combo_latched = 1;
                write_line("HOTKEY OK: L+R+SELECT detected", 30);
            }
        } else {
            combo_latched = 0;
        }

        if ((pad.Buttons & PSP_CTRL_TRIANGLE) && !(prev_buttons & PSP_CTRL_TRIANGLE))
            write_line("BUTTON OK: TRIANGLE detected", 28);
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(prev_buttons & PSP_CTRL_CIRCLE))
            write_line("BUTTON OK: CIRCLE detected", 26);

        prev_buttons = pad.Buttons;
        sceKernelDelayThread(20000);
    }

    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.3: module_start", 25);

    th = sceKernelCreateThread("RA-PSP Probe Worker", worker, 0x30, 0x2000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        write_line("ERROR: sceKernelCreateThread failed", 35);
        return 0;
    }

    if (sceKernelStartThread(th, 0, 0) < 0)
        write_line("ERROR: sceKernelStartThread failed", 34);
    else
        write_line("RA-PSP v0.3: worker launched", 28);

    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    write_line("RA-PSP v0.3: module_stop", 24);
    return 0;
}
