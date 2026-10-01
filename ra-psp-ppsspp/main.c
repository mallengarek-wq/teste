#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>

#include "rc_client.h"
#include "ra_network.h"
#include "ra_memory_psp.h"

PSP_MODULE_INFO("RA-PSP PPSSPP", PSP_MODULE_USER, 1, 61);
PSP_NO_CREATE_MAIN_THREAD();

#define LOG_PATH "ms0:/PSP/PLUGINS/RA-PSP/ra_psp_ppsspp.log"
#define AUTH_PATH "ms0:/PSP/PLUGINS/RA-PSP/auth.ini"
#define LOGIN_PATH "ms0:/PSP/PLUGINS/RA-PSP/login.ini"
#define LEGACY_AUTH_1 "ms0:/PSP/GAME/RA-PSP-LOGIN/auth.ini"
#define LEGACY_AUTH_2 "ms0:/SEPLUGINS/RA-PSP/auth.ini"
#define LEGACY_LOGIN_1 "ms0:/PSP/GAME/RA-PSP-LOGIN/login.ini"
#define LEGACY_LOGIN_2 "ms0:/SEPLUGINS/RA-PSP/login.ini"
#define COMBO_MAIN (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT)
#define COMBO_FALLBACK (PSP_CTRL_START | PSP_CTRL_SELECT)

static rc_client_t* g_client;
static volatile int g_config_state;    /* 0 unknown, 1 token, 2 password, -1 missing */
static volatile int g_login_state;     /* 0 idle, 1 pending, 2 ok, -1 failed */
static volatile int g_game_state;      /* 0 idle, 1 pending, 2 loaded, -1 failed */
static volatile int g_online;
static volatile int g_load_started;
static volatile int g_last_unlock_points;
static char g_username[64];
static char g_token[128];
static char g_password[128];
static char g_hash[40];
static char g_last_unlock[96];
static char g_last_error[128];

static int text_len(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void copy_text(char* dst, unsigned int size, const char* src) {
    unsigned int i = 0;
    if (!dst || size == 0) return;
    if (!src) { dst[0] = 0; return; }
    while (i + 1 < size && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void write_line(const char *text) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, text, text_len(text));
        sceIoWrite(fd, "\r\n", 2);
        sceIoClose(fd);
    }
}

static void log_code(const char* label, int code) {
    char b[96];
    snprintf(b, sizeof(b), "%s: %d (0x%08X)", label, code, (unsigned int)code);
    write_line(b);
}

static void trim(char* s) {
    int n;
    char* p;
    if (!s) return;
    while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
    n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
    p = strchr(s, '#');
    if (p) *p = 0;
}

static void clear_credentials(void) {
    memset(g_username, 0, sizeof(g_username));
    memset(g_token, 0, sizeof(g_token));
    memset(g_password, 0, sizeof(g_password));
    memset(g_hash, 0, sizeof(g_hash));
}

static int read_config_file(const char* path, int allow_password) {
    SceUID fd;
    char buf[1200];
    int n;
    char* line;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return -2;
    buf[n] = 0;

    line = strtok(buf, "\n");
    while (line) {
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            trim(line);
            trim(eq + 1);
            if (strcmp(line, "username") == 0) copy_text(g_username, sizeof(g_username), eq + 1);
            else if (strcmp(line, "token") == 0) copy_text(g_token, sizeof(g_token), eq + 1);
            else if (allow_password && strcmp(line, "password") == 0) copy_text(g_password, sizeof(g_password), eq + 1);
            else if (strcmp(line, "game_hash") == 0) copy_text(g_hash, sizeof(g_hash), eq + 1);
        }
        line = strtok(NULL, "\n");
    }

    if (!g_username[0] || strlen(g_hash) != 32) return -3;
    if (g_token[0]) return 1;
    if (allow_password && g_password[0]) return 2;
    return -4;
}

static int try_config(const char* path, int allow_password) {
    clear_credentials();
    return read_config_file(path, allow_password);
}

static int load_credentials(void) {
    int r;

    r = try_config(AUTH_PATH, 0);
    if (r == 1) return 1;
    r = try_config(LEGACY_AUTH_1, 0);
    if (r == 1) return 1;
    r = try_config(LEGACY_AUTH_2, 0);
    if (r == 1) return 1;
    r = try_config(LOGIN_PATH, 1);
    if (r == 1 || r == 2) return r;
    r = try_config(LEGACY_LOGIN_1, 1);
    if (r == 1 || r == 2) return r;
    r = try_config(LEGACY_LOGIN_2, 1);
    if (r == 1 || r == 2) {
        write_line("CONFIG: legacy SEPLUGINS login.ini found");
        return r;
    }
    clear_credentials();
    return -1;
}

static void save_token(void) {
    const rc_client_user_t* user;
    SceUID fd;
    char buf[420];
    int n;
    if (!g_client) return;
    user = rc_client_get_user_info(g_client);
    if (!user || !user->token || !user->token[0]) return;

    n = snprintf(buf, sizeof(buf), "username=%s\ntoken=%s\ngame_hash=%s\n", g_username, user->token, g_hash);
    fd = sceIoOpen(AUTH_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, buf, n);
        sceIoClose(fd);
        write_line("AUTH: token saved to plugin folder");
    }
    memset(g_password, 0, sizeof(g_password));
}

static void RC_CCONV on_event(const rc_client_event_t* event, rc_client_t* client) {
    (void)client;
    if (!event) return;
    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if (event->achievement) {
                copy_text(g_last_unlock, sizeof(g_last_unlock), event->achievement->title);
                g_last_unlock_points = (int)event->achievement->points;
                write_line("RA EVENT: achievement triggered");
            }
            break;
        case RC_CLIENT_EVENT_DISCONNECTED:
            g_online = 0;
            write_line("RA EVENT: disconnected");
            break;
        case RC_CLIENT_EVENT_RECONNECTED:
            g_online = 1;
            write_line("RA EVENT: reconnected");
            break;
        default:
            break;
    }
}

static void RC_CCONV on_login(int result, const char* error_message, rc_client_t* client, void* userdata) {
    (void)client; (void)userdata;
    if (result == RC_OK) {
        g_login_state = 2;
        g_online = 1;
        g_last_error[0] = 0;
        write_line("RA LOGIN: OK");
        if (g_config_state == 2) save_token();
    } else {
        g_login_state = -1;
        g_online = 0;
        copy_text(g_last_error, sizeof(g_last_error), error_message ? error_message : "login failed");
        write_line("RA LOGIN: FAILED");
        if (error_message) write_line(error_message);
    }
}

static void RC_CCONV on_game_loaded(int result, const char* error_message, rc_client_t* client, void* userdata) {
    (void)client; (void)userdata;
    if (result == RC_OK) {
        g_game_state = 2;
        g_last_error[0] = 0;
        write_line("RA GAME: SET LOADED");
    } else {
        g_game_state = -1;
        copy_text(g_last_error, sizeof(g_last_error), error_message ? error_message : "game load failed");
        write_line("RA GAME: LOAD FAILED");
        if (error_message) write_line(error_message);
    }
}

static int init_ra(void) {
    int rc;
    g_client = rc_client_create(ra_psp_read_memory, ra_net_server_call);
    if (!g_client) {
        copy_text(g_last_error, sizeof(g_last_error), "rc_client_create failed");
        return -1;
    }
    rc_client_set_event_handler(g_client, on_event);
    rc_client_set_hardcore_enabled(g_client, 0);
    rc_client_set_allow_background_memory_reads(g_client, 0);
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_WARN, 0);

    g_login_state = 1;
    if (g_config_state == 1)
        rc = rc_client_begin_login_with_token(g_client, g_username, g_token, on_login, 0) ? 0 : -2;
    else
        rc = rc_client_begin_login_with_password(g_client, g_username, g_password, on_login, 0) ? 0 : -2;

    if (rc < 0) {
        g_login_state = -1;
        copy_text(g_last_error, sizeof(g_last_error), "could not start login");
    }
    return rc;
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

static const char* config_label(void) {
    if (g_config_state == 1) return "TOKEN";
    if (g_config_state == 2) return "PASSWORD";
    return "AUSENTE";
}

static const char* login_label(void) {
    if (g_login_state == 2) return "OK";
    if (g_login_state == 1) return "CONECTANDO";
    if (g_login_state < 0) return "FALHOU";
    return "AGUARDANDO";
}

static void show_ra_menu(const char *hotkey_name) {
    pspUtilityMsgDialogParams dialog;
    pspTime now;
    char message[512];
    const rc_client_user_t* user = g_client ? rc_client_get_user_info(g_client) : 0;
    const rc_client_game_t* game = g_client ? rc_client_get_game_info(g_client) : 0;
    rc_client_user_game_summary_t summary;
    int loops = 0;
    int init_result;
    int neterr = ra_net_last_error();
    memset(&summary, 0, sizeof(summary));
    if (g_client && g_game_state == 2) rc_client_get_user_game_summary(g_client, &summary);

    configure_dialog(&dialog);
    sceRtcGetCurrentClockLocalTime(&now);

    if (g_game_state == 2 && game) {
        snprintf(message, sizeof(message),
            "RA-PSP PPSSPP v0.6.1 ONLINE\n\n"
            "%s\n"
            "Usuario: %s\n\n"
            "Config: %s   Login: %s\n"
            "Set: CARREGADO   Online: %s\n"
            "Conquistas: %u/%u\n"
            "Pontos: %u/%u\n\n"
            "Ultima: %s%s\n"
            "Hora: %02d:%02d\n\n"
            "Voltar fecha este menu.",
            game->title ? game->title : "PERSONA 2: INNOCENT SIN",
            user && user->display_name ? user->display_name : g_username,
            config_label(), login_label(), g_online ? "SIM" : "NAO",
            summary.num_unlocked_achievements, summary.num_core_achievements,
            summary.points_unlocked, summary.points_core,
            g_last_unlock[0] ? g_last_unlock : "nenhuma nesta sessao",
            g_last_unlock[0] ? "" : "",
            now.hour, now.minutes);
    } else {
        snprintf(message, sizeof(message),
            "RA-PSP PPSSPP v0.6.1 ONLINE\n\n"
            "PERSONA 2: INNOCENT SIN\n"
            "ULUS10584\n\n"
            "Config: %s\n"
            "Login: %s\n"
            "Set: %s\n"
            "Rede erro: %d\n\n"
            "%s%s\n\n"
            "login.ini aceito em:\n"
            "PSP/PLUGINS/RA-PSP ou SEPLUGINS/RA-PSP\n"
            "Hora: %02d:%02d\n\n"
            "Voltar fecha este menu.",
            config_label(), login_label(),
            g_game_state == 1 ? "CARREGANDO" : (g_game_state == -1 ? "FALHOU" : "AGUARDANDO"),
            neterr,
            g_last_error[0] ? "Erro: " : "",
            g_last_error[0] ? g_last_error : "",
            now.hour, now.minutes);
    }

    copy_text(dialog.message, sizeof(dialog.message), message);
    write_line(hotkey_name);
    init_result = sceUtilityMsgDialogInitStart(&dialog);
    if (init_result < 0) {
        log_code("MENU ERROR", init_result);
        return;
    }

    while (loops++ < 7200) {
        int status;
        sceDisplayWaitVblankStart();
        status = sceUtilityMsgDialogGetStatus();
        if (status == PSP_UTILITY_DIALOG_VISIBLE)
            sceUtilityMsgDialogUpdate(1);
        else if (status == PSP_UTILITY_DIALOG_QUIT)
            sceUtilityMsgDialogShutdownStart();
        else if (status == PSP_UTILITY_DIALOG_NONE && loops > 5)
            break;
        sceKernelDelayThread(500);
    }
}

static int worker(SceSize args, void *argp) {
    SceCtrlData pad;
    int main_latched = 0;
    int fallback_latched = 0;
    int frame_counter = 0;
    (void)args; (void)argp;

    write_line("RA-PSP v0.6.1: worker started");
    sceKernelDelayThread(1500000);

    g_config_state = load_credentials();
    if (g_config_state > 0) {
        write_line(g_config_state == 1 ? "CONFIG: token credentials found" : "CONFIG: password login found");
        init_ra();
    } else {
        write_line("CONFIG: auth.ini/login.ini missing or incomplete");
        copy_text(g_last_error, sizeof(g_last_error), "credenciais ausentes");
    }

    for (;;) {
        if (g_client) {
            if (g_login_state == 2 && !g_load_started) {
                g_load_started = 1;
                g_game_state = 1;
                if (!rc_client_begin_load_game(g_client, g_hash, on_game_loaded, 0)) {
                    g_game_state = -1;
                    copy_text(g_last_error, sizeof(g_last_error), "could not start game load");
                }
            }
            if (g_game_state == 2) {
                rc_client_do_frame(g_client);
                if (++frame_counter >= 600) {
                    frame_counter = 0;
                    write_line("RA RUNTIME: active");
                }
            }
        }

        memset(&pad, 0, sizeof(pad));
        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            unsigned int buttons = pad.Buttons;
            if ((buttons & COMBO_MAIN) == COMBO_MAIN) {
                if (!main_latched) {
                    main_latched = 1;
                    show_ra_menu("HOTKEY OK: L+R+SELECT detected");
                }
            } else main_latched = 0;

            if ((buttons & COMBO_FALLBACK) == COMBO_FALLBACK) {
                if (!fallback_latched) {
                    fallback_latched = 1;
                    show_ra_menu("HOTKEY OK: START+SELECT detected");
                }
            } else fallback_latched = 0;
        }

        sceKernelDelayThread(16667);
    }
    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th;
    (void)args; (void)argp;
    write_line("RA-PSP v0.6.1: module_start");
    th = sceKernelCreateThread("RA-PSP PPSSPP Worker", worker, 0x34, 0x9000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        log_code("ERROR: sceKernelCreateThread", th);
        return 0;
    }
    if (sceKernelStartThread(th, 0, 0) < 0)
        write_line("ERROR: sceKernelStartThread failed");
    else
        write_line("RA-PSP v0.6.1: worker launched");
    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    if (g_client) {
        rc_client_unload_game(g_client);
        rc_client_destroy(g_client);
        g_client = 0;
    }
    ra_net_shutdown();
    write_line("RA-PSP v0.6.1: module_stop");
    return 0;
}