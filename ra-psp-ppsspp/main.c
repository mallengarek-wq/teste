#include <pspkernel.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP PPSSPP Probe", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define LOG_PATH "ms0:/PSP/PLUGINS/RA-PSP/ra_psp_ppsspp.log"
#define COMBO (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT)

static volatile int g_running;

static void append_log(const char *text) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, text, (SceSize)strlen(text));
        sceIoClose(fd);
    }
}

static int worker(SceSize args, void *argp) {
    SceCtrlData pad;
    unsigned int prev_buttons = 0;
    int combo_latched = 0;
    (void)args;
    (void)argp;

    append_log("RA-PSP PPSSPP probe: worker started\r\n");
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (g_running) {
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(&pad, 1);

        if ((pad.Buttons & COMBO) == COMBO) {
            if (!combo_latched) {
                combo_latched = 1;
                append_log("HOTKEY OK: L+R+SELECT detected\r\n");
            }
        } else {
            combo_latched = 0;
        }

        if ((pad.Buttons & PSP_CTRL_TRIANGLE) && !(prev_buttons & PSP_CTRL_TRIANGLE))
            append_log("BUTTON OK: TRIANGLE detected\r\n");
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(prev_buttons & PSP_CTRL_CIRCLE))
            append_log("BUTTON OK: CIRCLE detected\r\n");

        prev_buttons = pad.Buttons;
        sceKernelDelayThread(20000);
    }

    append_log("RA-PSP PPSSPP probe: worker stopped\r\n");
    sceKernelExitDeleteThread(0);
    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th;
    (void)args;
    (void)argp;

    g_running = 1;
    append_log("RA-PSP PPSSPP probe: module_start\r\n");

    th = sceKernelCreateThread("RA-PSP Probe Worker", worker, 0x30, 0x2000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        append_log("ERROR: sceKernelCreateThread failed\r\n");
        return 0;
    }

    if (sceKernelStartThread(th, 0, 0) < 0)
        append_log("ERROR: sceKernelStartThread failed\r\n");

    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    g_running = 0;
    append_log("RA-PSP PPSSPP probe: module_stop\r\n");
    return 0;
}
