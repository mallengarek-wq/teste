#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP Simulation", PSP_MODULE_USER, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define LOG_PATH "ms0:/PSP/RA_SIM_LOG.txt"
#define COL_WHITE   0x00F4F7FF
#define COL_MUTED   0x0096A6B8
#define COL_CYAN    0x00E8C48A
#define COL_GREEN   0x0098E0A0
#define COL_YELLOW  0x0068D8F8
#define COL_RED     0x007070F0
#define BG_MAIN     0x00302014
#define BG_PANEL    0x00503820
#define BG_SELECT   0x00806030
#define BG_POPUP    0x00604020

static int progress = 0;
static int unlocked = 0;
static int queued = 0;
static int synced = 0;
static int online = 0;
static int popup_frames = 0;
static int menu_open = 0;
static int menu_index = 0;
static int filter_mode = 0; /* 0 all, 1 unlocked, 2 locked */

static const char *ach_names[] = {
    "First Contact",
    "Rumor Monger",
    "Friends, Again",
    "City of Seven Sisters",
    "Under the Same Moon",
    "Another Possibility"
};

static const char *ach_desc[] = {
    "Reach memory value 10.",
    "Trigger your first rumor.",
    "Reunite with all party members.",
    "Explore all Sumaru districts.",
    "View every Moon rumor.",
    "Clear the game."
};

static const int ach_points[] = {5, 10, 30, 20, 30, 50};
static const int ach_unlocked_static[] = {0, 1, 1, 1, 0, 0};

static void log_line(const char *line) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, strlen(line));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void clear_screen(void) {
    pspDebugScreenSetBackColor(BG_MAIN);
    pspDebugScreenSetTextColor(COL_WHITE);
    pspDebugScreenClear();
}

static void print_at(int x, int y, unsigned int color, unsigned int bg, const char *text) {
    pspDebugScreenSetXY(x, y);
    pspDebugScreenSetTextColor(color);
    pspDebugScreenSetBackColor(bg);
    pspDebugScreenPrintf("%s", text);
}

static int achievement_unlocked(int index) {
    if (index == 0) return unlocked;
    return ach_unlocked_static[index];
}

static int visible_count(void) {
    int i, n = 0;
    for (i = 0; i < 6; ++i) {
        int u = achievement_unlocked(i);
        if (filter_mode == 0 || (filter_mode == 1 && u) || (filter_mode == 2 && !u)) n++;
    }
    return n;
}

static int visible_to_real(int visible_index) {
    int i, n = 0;
    for (i = 0; i < 6; ++i) {
        int u = achievement_unlocked(i);
        if (filter_mode == 0 || (filter_mode == 1 && u) || (filter_mode == 2 && !u)) {
            if (n == visible_index) return i;
            n++;
        }
    }
    return 0;
}

static void draw_header(const char *section) {
    char buf[64];
    print_at(1, 0, COL_WHITE, BG_PANEL, "  RA-PSP   ");
    snprintf(buf, sizeof(buf), " %-30s", section);
    print_at(10, 0, COL_MUTED, BG_PANEL, buf);
    print_at(49, 0, online ? COL_GREEN : COL_YELLOW, BG_PANEL, online ? " ONLINE " : " OFFLINE");

    print_at(0, 1, COL_CYAN, BG_MAIN,
        "------------------------------------------------------------");
}

static void draw_popup(void) {
    char line[64];
    if (popup_frames <= 0) return;

    print_at(27, 3, COL_CYAN, BG_POPUP, "+-----------------------------+");
    print_at(27, 4, COL_WHITE, BG_POPUP, "|  Achievement Unlocked       |");
    print_at(27, 5, COL_WHITE, BG_POPUP, "|  First Contact              |");
    print_at(27, 6, COL_MUTED, BG_POPUP, "|  Reach memory value 10      |");
    snprintf(line, sizeof(line), "|  %-15s          +5 |","" );
    (void)line;
    print_at(27, 7, online || synced ? COL_GREEN : COL_YELLOW, BG_POPUP,
        online || synced ? "|  SYNCED                  +5  |" : "|  SAVED LOCAL             +5  |");
    print_at(27, 8, COL_CYAN, BG_POPUP, "+-----------------------------+");
}

static void draw_game_view(void) {
    char buf[64];
    clear_screen();
    draw_header("Game");

    print_at(2, 4, COL_WHITE, BG_MAIN, "Persona-like PSP Test");
    print_at(2, 6, COL_MUTED, BG_MAIN, "Achievement runtime simulation");

    snprintf(buf, sizeof(buf), "Memory trigger  0x00001000 = %02d / 10", progress);
    print_at(2, 10, COL_WHITE, BG_MAIN, buf);

    print_at(2, 12, unlocked ? COL_GREEN : COL_MUTED, BG_MAIN,
        unlocked ? "[x] First Contact" : "[ ] First Contact");

    snprintf(buf, sizeof(buf), "Queue status: %s", synced ? "SYNCED" : (queued ? "PENDING" : "EMPTY"));
    print_at(2, 14, queued ? COL_YELLOW : COL_MUTED, BG_MAIN, buf);

    print_at(2, 19, COL_MUTED, BG_MAIN, "X  Progress        TRIANGLE  Network");
    print_at(2, 21, COL_MUTED, BG_MAIN, "L+R+SELECT  Achievements     O  Exit");

    draw_popup();
}

static void draw_menu(void) {
    char buf[64];
    int total_unlocked = 3 + (unlocked ? 1 : 0);
    int visible = visible_count();
    int row;

    clear_screen();
    draw_header("Achievements");

    snprintf(buf, sizeof(buf), "Persona-like PSP Test    %d / 6 unlocked", total_unlocked);
    print_at(2, 3, COL_WHITE, BG_MAIN, buf);

    print_at(2, 5, filter_mode == 0 ? COL_WHITE : COL_MUTED, filter_mode == 0 ? BG_SELECT : BG_PANEL, " ALL ");
    print_at(9, 5, filter_mode == 1 ? COL_WHITE : COL_MUTED, filter_mode == 1 ? BG_SELECT : BG_PANEL, " UNLOCKED ");
    print_at(21, 5, filter_mode == 2 ? COL_WHITE : COL_MUTED, filter_mode == 2 ? BG_SELECT : BG_PANEL, " LOCKED ");

    print_at(0, 7, COL_CYAN, BG_MAIN,
        "------------------------------------------------------------");

    for (row = 0; row < visible && row < 6; ++row) {
        int idx = visible_to_real(row);
        int y = 9 + row * 3;
        int u = achievement_unlocked(idx);
        unsigned int bg = row == menu_index ? BG_SELECT : BG_MAIN;
        unsigned int fg = row == menu_index ? COL_WHITE : (u ? COL_WHITE : COL_MUTED);

        snprintf(buf, sizeof(buf), "%c %-33s %3d pts", u ? '*' : 'o', ach_names[idx], ach_points[idx]);
        print_at(3, y, fg, bg, buf);

        snprintf(buf, sizeof(buf), "   %-47s", ach_desc[idx]);
        print_at(3, y + 1, row == menu_index ? COL_CYAN : COL_MUTED, bg, buf);
    }

    print_at(0, 28, COL_CYAN, BG_MAIN,
        "------------------------------------------------------------");
    print_at(2, 30, COL_MUTED, BG_MAIN, "UP/DOWN  Select    L/R  Filter    O  Back");

    if (visible == 0) {
        print_at(17, 13, COL_MUTED, BG_MAIN, "No achievements in this filter.");
    }
}

static void evaluate(void) {
    if (!unlocked && progress >= 10) {
        unlocked = 1;
        popup_frames = 300;
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
        popup_frames = 220;
        log_line("SYNC achievement=1001 status=confirmed_after_reconnect");
    }
}

int main(int argc, char *argv[]) {
    SceCtrlData pad, old;
    (void)argc; (void)argv;

    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    log_line("BOOT RA-PSP native-ui simulation started");
    memset(&old, 0, sizeof(old));

    while (1) {
        unsigned int pressed;
        sceCtrlReadBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~old.Buttons;

        if (!menu_open) {
            if (pressed & PSP_CTRL_CROSS) {
                if (progress < 10) progress++;
                evaluate();
            }
            if (pressed & PSP_CTRL_TRIANGLE) {
                online = !online;
                log_line(online ? "NETWORK online" : "NETWORK offline");
                evaluate();
            }
            if ((pad.Buttons & PSP_CTRL_LTRIGGER) &&
                (pad.Buttons & PSP_CTRL_RTRIGGER) &&
                (pressed & PSP_CTRL_SELECT)) {
                menu_open = 1;
                menu_index = 0;
                log_line("UI achievement_browser_open");
            }
            if (pressed & PSP_CTRL_CIRCLE) break;
        } else {
            int count = visible_count();
            if (pressed & PSP_CTRL_UP) {
                if (count > 0) menu_index = (menu_index + count - 1) % count;
            }
            if (pressed & PSP_CTRL_DOWN) {
                if (count > 0) menu_index = (menu_index + 1) % count;
            }
            if (pressed & PSP_CTRL_LTRIGGER) {
                filter_mode = (filter_mode + 2) % 3;
                menu_index = 0;
            }
            if (pressed & PSP_CTRL_RTRIGGER) {
                filter_mode = (filter_mode + 1) % 3;
                menu_index = 0;
            }
            if (pressed & PSP_CTRL_CIRCLE) {
                menu_open = 0;
                log_line("UI achievement_browser_close");
            }
        }

        if (popup_frames > 0) popup_frames--;
        if (menu_open) draw_menu(); else draw_game_view();
        old = pad;
        sceKernelDelayThread(16666);
    }

    sceKernelExitGame();
    return 0;
}
