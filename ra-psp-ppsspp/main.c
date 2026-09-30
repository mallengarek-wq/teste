#include <pspkernel.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP PPSSPP", PSP_MODULE_USER, 1, 4);
PSP_NO_CREATE_MAIN_THREAD();

#define LOG_PATH "ms0:/PSP/PLUGINS/RA-PSP/ra_psp_ppsspp.log"
#define COMBO_MAIN (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT)
#define COMBO_FALLBACK (PSP_CTRL_START | PSP_CTRL_SELECT)

static int text_len(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void write_line(const char *text) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, text, text_len(text));
        sceIoWrite(fd, "\r\n", 2);
        sceIoClose(fd);
    }
}

static void show_confirmation(const char *which) {
    pspUtilityMsgDialogParams dialog;
    int init_result;
    int loops = 0;

    memset(&dialog, 0, sizeof(dialog));
    dialog.base.size = sizeof(dialog);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &dialog.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &dialog.base.buttonSwap);
    dialog.base.graphicsThread = 0x11;
    dialog.base.accessThread = 0x13;
    dialog.base.fontThread = 0x12;
    dialog.base.soundThread = 0x10;
    dialog.mode = PSP_UTILITY_MSGDIALOG_MODE_TEXT;
    dialog.options = PSP_UTILITY_MSGDIALOG_OPTION_TEXT;

    strncpy(dialog.message,
            "RA-PSP PPSSPP v0.4\n\n"
            "Atalho detectado corretamente.\n"
            "O plugin esta lendo os controles do jogo.\n\n"
            "Feche esta caixa para continuar.",
            sizeof(dialog.message) - 1);
    dialog.message[sizeof(dialog.message) - 1] = '\0';

    write_line(which);
    init_result = sceUtilityMsgDialogInitStart(&dialog);
    if (init_result < 0) {
        write_line("DIALOG ERROR: sceUtilityMsgDialogInitStart failed");
        return;
    }

    write_line("DIALOG OK: visible confirmation requested");

    while (loops++ < 3600) {
        int status = sceUtilityMsgDialogGetStatus();
        if (status == PSP_UTILITY_DIALOG_VISIBLE) {
            sceUtilityMsgDialogUpdate(1);
        } else if (status == PSP_UTILITY_DIALOG_QUIT) {
            sceUtilityMsgDialogShutdownStart();
        } else if (status == PSP_UTILITY_DIALOG_NONE && loops > 5) {
            break;
        }
        sceKernelDelayThread(16667);
    }

    write_line("DIALOG CLOSED");
}

static int worker(SceSize args, void *argp) {
    SceCtrlData pad;
    unsigned int previous = 0;
    int main_latched = 0;
    int fallback_latched = 0;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.4: worker started");

    for (;;) {
        memset(&pad, 0, sizeof(pad));
        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            unsigned int buttons = pad.Buttons;

            if ((buttons & COMBO_MAIN) == COMBO_MAIN) {
                if (!main_latched) {
                    main_latched = 1;
                    show_confirmation("HOTKEY OK: L+R+SELECT detected");
                }
            } else {
                main_latched = 0;
            }

            if ((buttons & COMBO_FALLBACK) == COMBO_FALLBACK) {
                if (!fallback_latched) {
                    fallback_latched = 1;
                    show_confirmation("HOTKEY OK: START+SELECT detected");
                }
            } else {
                fallback_latched = 0;
            }

            if ((buttons & PSP_CTRL_TRIANGLE) && !(previous & PSP_CTRL_TRIANGLE))
                write_line("BUTTON OK: TRIANGLE detected");
            if ((buttons & PSP_CTRL_CIRCLE) && !(previous & PSP_CTRL_CIRCLE))
                write_line("BUTTON OK: CIRCLE detected");

            previous = buttons;
        }

        sceKernelDelayThread(16667);
    }

    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.4: module_start");

    th = sceKernelCreateThread("RA-PSP PPSSPP Worker", worker, 0x30, 0x4000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        write_line("ERROR: sceKernelCreateThread failed");
        return 0;
    }

    if (sceKernelStartThread(th, 0, 0) < 0)
        write_line("ERROR: sceKernelStartThread failed");
    else
        write_line("RA-PSP v0.4: worker launched");

    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    write_line("RA-PSP v0.4: module_stop");
    return 0;
}
