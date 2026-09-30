#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <stdio.h>

PSP_MODULE_INFO("RA_PSP_DIAG", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define printf pspDebugScreenPrintf

static int running = 1;

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static int setup_callbacks(void) {
    int thid = sceKernelCreateThread("callback_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
    return thid;
}

static void run_tests(void) {
    volatile unsigned int probe = 0x12345678;
    int ram_ok = (probe == 0x12345678);
    int file_ok = 0;

    FILE *f = fopen("ms0:/PSP/RA_PSP_DIAG_TEST.txt", "w");
    if (f) {
        fprintf(f, "RA-PSP diagnostic OK\nRAM=%s\n", ram_ok ? "OK" : "FAIL");
        fclose(f);
        file_ok = 1;
    }

    pspDebugScreenClear();
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    printf("RA-PSP DIAGNOSTIC v0.0.2\n");
    printf("=========================\n\n");

    pspDebugScreenSetTextColor(ram_ok ? 0xFF00FF00 : 0xFF0000FF);
    printf("[ %s ] CPU / RAM basic access\n", ram_ok ? "OK" : "FAIL");

    pspDebugScreenSetTextColor(0xFF00FF00);
    printf("[ OK ] PSP display API initialized\n");
    printf("[ OK ] Framebuffer registered by PSPSDK\n");

    pspDebugScreenSetTextColor(file_ok ? 0xFF00FF00 : 0xFF0000FF);
    printf("[ %s ] Memory Stick file write\n", file_ok ? "OK" : "FAIL");

    pspDebugScreenSetTextColor(0xFF00FFFF);
    printf("[WAIT] rcheevos not linked yet\n");
    printf("[WAIT] RetroAchievements network not linked yet\n\n");

    pspDebugScreenSetTextColor(0xFFFFFFFF);
    printf("X = run tests again\n");
    printf("O = exit\n\n");
    printf("If you can read this screen, the PPSSPP test layer is working.\n");
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    setup_callbacks();

    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    run_tests();

    SceCtrlData pad, oldpad;
    oldpad.Buttons = 0;

    while (running) {
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldpad.Buttons;

        if (pressed & PSP_CTRL_CROSS) run_tests();
        if (pressed & PSP_CTRL_CIRCLE) break;

        oldpad = pad;
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
