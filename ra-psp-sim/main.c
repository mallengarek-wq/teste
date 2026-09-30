#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP Simulation", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define LOG_PATH "ms0:/PSP/RA_SIM_LOG.txt"

static int progress = 0;
static int unlocked = 0;
static int queued = 0;
static int synced = 0;
static int online = 0;
static int popup_frames = 0;

static void log_line(const char *line) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, strlen(line));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void draw(void) {
    pspDebugScreenClear();
    pspDebugScreenSetXY(2,1);
    pspDebugScreenSetTextColor(0x00FFFFFF);
    pspDebugScreenPrintf("RA-PSP FLOW SIMULATOR v0.1\n");
    pspDebugScreenPrintf("==========================\n\n");

    pspDebugScreenSetTextColor(0x0000FF00);
    pspDebugScreenPrintf("[OK] GAME-mode runtime\n");
    pspDebugScreenPrintf("[OK] memory reader\n");
    pspDebugScreenPrintf("[OK] local achievement evaluator\n");
    pspDebugScreenPrintf("[OK] persistent offline queue\n\n");

    pspDebugScreenSetTextColor(0x00FFFFFF);
    pspDebugScreenPrintf("Game: Persona-like PSP test\n");
    pspDebugScreenPrintf("Fake RA memory 0x00001000 = %d / 10\n", progress);
    pspDebugScreenPrintf("Network: %s\n", online ? "ONLINE" : "OFFLINE");
    pspDebugScreenPrintf("Achievement: %s\n", unlocked ? "UNLOCKED" : "LOCKED");
    pspDebugScreenPrintf("Queue: %s\n\n", synced ? "SYNCED" : (queued ? "PENDING" : "EMPTY"));

    pspDebugScreenSetTextColor(0x00FFFF00);
    pspDebugScreenPrintf("X = increase simulated game progress\n");
    pspDebugScreenPrintf("TRIANGLE = toggle internet\n");
    pspDebugScreenPrintf("SQUARE = reset scenario\n");
    pspDebugScreenPrintf("CIRCLE = exit\n\n");

    if (popup_frames > 0) {
        pspDebugScreenSetTextColor(0x0000FFFF);
        pspDebugScreenPrintf("+--------------------------------------+\n");
        pspDebugScreenPrintf("|  ACHIEVEMENT UNLOCKED!                |\n");
        pspDebugScreenPrintf("|  First Contact                        |\n");
        pspDebugScreenPrintf("|  Reach memory value 10                |\n");
        pspDebugScreenPrintf("|  %s                    |\n", online ? "SYNCED ONLINE " : "SAVED OFFLINE");
        pspDebugScreenPrintf("+--------------------------------------+\n");
    }

    if (online && queued && !synced) {
        pspDebugScreenSetTextColor(0x0000FF00);
        pspDebugScreenPrintf("\nCloud sync: pending unlock detected...\n");
    }
}

static void evaluate(void) {
    if (!unlocked && progress >= 10) {
        unlocked = 1;
        popup_frames = 240;
        log_line("UNLOCK achievement=1001 title=First_Contact");
        if (online) {
            synced = 1;
            log_line("SYNC achievement=1001 status=confirmed");
        } else {
            queued = 1;
            log_line("QUEUE achievement=1001 state=pending");
        }
    }

    if (online && queued && !synced) {
        synced = 1;
        queued = 0;
        popup_frames = 180;
        log_line("SYNC achievement=1001 status=confirmed_after_reconnect");
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    log_line("BOOT RA-PSP simulation started");

    SceCtrlData pad, old;
    memset(&old, 0, sizeof(old));

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~old.Buttons;

        if (pressed & PSP_CTRL_CROSS) {
            if (progress < 10) progress++;
            evaluate();
        }
        if (pressed & PSP_CTRL_TRIANGLE) {
            online = !online;
            log_line(online ? "NETWORK online" : "NETWORK offline");
            evaluate();
        }
        if (pressed & PSP_CTRL_SQUARE) {
            progress = unlocked = queued = synced = popup_frames = 0;
            online = 0;
            log_line("RESET scenario");
        }
        if (pressed & PSP_CTRL_CIRCLE) break;

        if (popup_frames > 0) popup_frames--;
        draw();
        old = pad;
        sceKernelDelayThread(16666);
    }

    sceKernelExitGame();
    return 0;
}
