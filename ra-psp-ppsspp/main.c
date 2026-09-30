#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP PPSSPP", PSP_MODULE_USER, 1, 53);
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

static void configure_dialog(pspUtilityMsgDialogParams *dialog) {
    memset(dialog, 0, sizeof(*dialog));
    dialog->base.size = sizeof(*dialog);
    dialog->base.language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
    dialog->base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;
    dialog->base.graphicsThread = 0x11;
    dialog->base.accessThread = 0x13;
    dialog->base.fontThread = 0x12;
    dialog->base.soundThread = 0x10;
    dialog->mode = PSP_UTILITY_MSGDIALOG_MODE_TEXT;
    dialog->options = PSP_UTILITY_MSGDIALOG_OPTION_TEXT;
}

static void show_ra_menu(const char *hotkey_name) {
    pspUtilityMsgDialogParams dialog;
    pspTime now;
    char message[512];
    int init_result;
    int loops = 0;

    configure_dialog(&dialog);

    if (sceRtcGetCurrentClockLocalTime(&now) >= 0) {
        snprintf(message, sizeof(message),
            "RA-PSP PPSSPP v0.5.3\n\n"
            "PERSONA 2: INNOCENT SIN\n"
            "ULUS10584\n\n"
            "Plugin............. OK\n"
            "Controles.......... OK\n"
            "Menu nativo........ OK\n"
            "Sincronizacao...... VBLANK\n"
            "RetroAchievements.. OFF\n"
            "Set carregado...... NAO\n\n"
            "Hora: %02d:%02d\n\n"
            "Proxima etapa: login + rc_client + set real.\n"
            "Voltar fecha este menu.",
            now.hour, now.minutes);
    } else {
        snprintf(message, sizeof(message),
            "RA-PSP PPSSPP v0.5.3\n\n"
            "PERSONA 2: INNOCENT SIN\n"
            "ULUS10584\n\n"
            "Plugin............. OK\n"
            "Controles.......... OK\n"
            "Menu nativo........ OK\n"
            "Sincronizacao...... VBLANK\n"
            "RetroAchievements.. OFF\n"
            "Set carregado...... NAO\n\n"
            "Proxima etapa: login + rc_client + set real.\n"
            "Voltar fecha este menu.");
    }

    strncpy(dialog.message, message, sizeof(dialog.message) - 1);
    dialog.message[sizeof(dialog.message) - 1] = '\0';

    write_line(hotkey_name);
    init_result = sceUtilityMsgDialogInitStart(&dialog);
    if (init_result < 0) {
        write_line("MENU ERROR: sceUtilityMsgDialogInitStart failed");
        return;
    }

    write_line("MENU OPEN: native utility dialog vblank synced");

    while (loops++ < 7200) {
        int status;

        /*
         * The previous build updated the utility dialog on a free-running
         * 16.7 ms timer. That can land before or after the game's own frame
         * and causes alternating game/dialog frames in PPSSPP. Wait for the
         * real display vblank instead. The RA worker has a lower priority than
         * typical game render threads, so the game gets its frame first and
         * the dialog update is submitted afterwards.
         */
        sceDisplayWaitVblankStart();
        status = sceUtilityMsgDialogGetStatus();

        if (status == PSP_UTILITY_DIALOG_VISIBLE) {
            sceUtilityMsgDialogUpdate(1);
        } else if (status == PSP_UTILITY_DIALOG_QUIT) {
            sceUtilityMsgDialogShutdownStart();
        } else if (status == PSP_UTILITY_DIALOG_NONE && loops > 5) {
            break;
        }

        /* Small yield only; frame pacing comes from VBlank above. */
        sceKernelDelayThread(500);
    }

    write_line("MENU CLOSED: native utility dialog");
}

static int worker(SceSize args, void *argp) {
    SceCtrlData pad;
    int main_latched = 0;
    int fallback_latched = 0;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.5.3: worker started");

    for (;;) {
        memset(&pad, 0, sizeof(pad));
        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            unsigned int buttons = pad.Buttons;

            if ((buttons & COMBO_MAIN) == COMBO_MAIN) {
                if (!main_latched) {
                    main_latched = 1;
                    show_ra_menu("HOTKEY OK: L+R+SELECT detected");
                }
            } else {
                main_latched = 0;
            }

            if ((buttons & COMBO_FALLBACK) == COMBO_FALLBACK) {
                if (!fallback_latched) {
                    fallback_latched = 1;
                    show_ra_menu("HOTKEY OK: START+SELECT detected");
                }
            } else {
                fallback_latched = 0;
            }
        }

        sceKernelDelayThread(16667);
    }

    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.5.3: module_start");

    th = sceKernelCreateThread("RA-PSP PPSSPP Worker", worker, 0x34, 0x5000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        write_line("ERROR: sceKernelCreateThread failed");
        return 0;
    }

    if (sceKernelStartThread(th, 0, 0) < 0)
        write_line("ERROR: sceKernelStartThread failed");
    else
        write_line("RA-PSP v0.5.3: worker launched");

    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    write_line("RA-PSP v0.5.3: module_stop");
    return 0;
}
