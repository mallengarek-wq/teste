#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP Native UI", PSP_MODULE_USER, 1, 5);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define LOG_PATH "ms0:/PSP/RA_SIM_LOG.txt"

/* ABGR colors used by pspDebugScreen */
#define C_WHITE      0x00F7FAFF
#define C_TEXT       0x00DCE8F5
#define C_SOFT       0x00AABCD0
#define C_DIM        0x006F8194
#define C_ICE        0x00FFD8A8
#define C_BLUE       0x00F0C892
#define C_GREEN      0x009BE4AF
#define C_YELLOW     0x0070D8F8
#define C_LOCK       0x00798B9E
#define BG           0x0033261A
#define BG_TOP       0x00513D28
#define BG_PANEL     0x00433224
#define BG_ROW       0x00473527
#define BG_FOCUS     0x00926B39
#define BG_TAB       0x0058452E
#define BG_POP       0x00654B2F

static int progress = 0;
static int unlocked = 0;
static int queued = 0;
static int synced = 0;
static int online = 0;
static int popup_frames = 0;
static int dirty = 1;
static int section = 1; /* 0 game, 1 achievements, 2 profile, 3 settings */
static int item = 0;
static int filter_mode = 0; /* all / unlocked / locked */

static const char *sections[] = {"GAME", "ACHIEVEMENTS", "PROFILE", "SETTINGS"};
static const char *ach_names[] = {
    "Another Possibility",
    "Friends, Again",
    "The Rumor Never Dies",
    "City of Seven Sisters",
    "Under the Same Moon",
    "A Familiar Face"
};
static const char *ach_desc[] = {
    "Cleared the game.",
    "Reunited with all party members.",
    "Witnessed the True End.",
    "Explored all areas of Sumaru City.",
    "Viewed every Snow Queen rumor.",
    "Spoke with the mysterious cat."
};
static const int ach_points[] = {50, 30, 50, 20, 30, 10};
static const int ach_static[] = {1, 1, 0, 1, 0, 1};

static void log_line(const char *s) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, s, strlen(s));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void cls(void) {
    pspDebugScreenSetBackColor(BG);
    pspDebugScreenSetTextColor(C_WHITE);
    pspDebugScreenClear();
}

static void txt(int x, int y, unsigned int fg, unsigned int bg, const char *s) {
    pspDebugScreenSetXY(x, y);
    pspDebugScreenSetTextColor(fg);
    pspDebugScreenSetBackColor(bg);
    pspDebugScreenPrintf("%s", s);
}

static void hline(int y, unsigned int color) {
    txt(0, y, color, BG, "------------------------------------------------------------");
}

static int ach_unlocked(int i) {
    if (i == 2) return unlocked;
    return ach_static[i];
}

static int visible_count(void) {
    int i, n = 0;
    for (i = 0; i < 6; ++i) {
        int u = ach_unlocked(i);
        if (filter_mode == 0 || (filter_mode == 1 && u) || (filter_mode == 2 && !u)) n++;
    }
    return n;
}

static int visible_to_real(int v) {
    int i, n = 0;
    for (i = 0; i < 6; ++i) {
        int u = ach_unlocked(i);
        if (filter_mode == 0 || (filter_mode == 1 && u) || (filter_mode == 2 && !u)) {
            if (n == v) return i;
            n++;
        }
    }
    return 0;
}

static int total_unlocked(void) {
    int i, n = 0;
    for (i = 0; i < 6; ++i) if (ach_unlocked(i)) n++;
    return n;
}

static void draw_top(void) {
    int i, x = 2;
    txt(1, 0, C_WHITE, BG_TOP, " GAME  ");
    txt(48, 0, C_TEXT, BG_TOP, online ? "3/14 17:26  WIFI" : "3/14 17:26  OFF ");
    hline(1, C_DIM);

    for (i = 0; i < 4; ++i) {
        char b[22];
        unsigned int fg = i == section ? C_WHITE : C_DIM;
        unsigned int bg = i == section ? BG_FOCUS : BG;
        snprintf(b, sizeof(b), " %s ", sections[i]);
        txt(x, 2, fg, bg, b);
        x += (int)strlen(sections[i]) + 4;
    }
}

static void draw_game_header(void) {
    char b[64];
    int pct = (total_unlocked() * 100) / 6;

    txt(3, 4, C_WHITE, BG_PANEL, " [ PERSONA 2 ] ");
    txt(3, 5, C_SOFT, BG_PANEL, " ETERNAL PUNISHMENT ");
    txt(21, 4, C_WHITE, BG, "Persona 2: Eternal Punishment");

    snprintf(b, sizeof(b), "%d / 6 unlocked", total_unlocked());
    txt(21, 6, C_TEXT, BG, b);

    txt(37, 6, C_DIM, BG, "[");
    txt(38, 6, C_BLUE, BG, pct >= 16 ? "====" : "    ");
    txt(42, 6, C_BLUE, BG, pct >= 50 ? "====" : "    ");
    txt(46, 6, C_DIM, BG, "]");
    snprintf(b, sizeof(b), " %d%%", pct);
    txt(48, 6, C_TEXT, BG, b);

    txt(50, 4, C_WHITE, BG_PANEL, " [:) ] ");
    txt(50, 5, C_TEXT, BG_PANEL, " Tatsuya ");
    txt(50, 6, C_SOFT, BG_PANEL, " Lv. 28 ");
}

static void draw_tabs(void) {
    txt(18, 8, filter_mode == 0 ? C_WHITE : C_SOFT,
        filter_mode == 0 ? BG_FOCUS : BG_TAB, "      All      ");
    txt(33, 8, filter_mode == 1 ? C_WHITE : C_SOFT,
        filter_mode == 1 ? BG_FOCUS : BG_TAB, "   Unlocked   ");
    txt(47, 8, filter_mode == 2 ? C_WHITE : C_SOFT,
        filter_mode == 2 ? BG_FOCUS : BG_TAB, "    Locked    ");
    hline(9, C_DIM);
}

static void draw_achievement_row(int row, int idx) {
    char line1[64], line2[64];
    int y = 10 + row * 3;
    int u = ach_unlocked(idx);
    unsigned int bg = row == item ? BG_FOCUS : BG_ROW;
    unsigned int fg = row == item ? C_WHITE : (u ? C_TEXT : C_DIM);
    const char *mark = u ? "[OK]" : "[LOCK]";

    snprintf(line1, sizeof(line1), " %-3s  %-28s %4d pts ", row == item ? ">" : " ", ach_names[idx], ach_points[idx]);
    txt(14, y, fg, bg, line1);

    snprintf(line2, sizeof(line2), "      %-30s %-6s ", ach_desc[idx], mark);
    txt(14, y + 1, row == item ? C_ICE : (u ? C_SOFT : C_LOCK), bg, line2);
}

static void draw_achievements(void) {
    int row, vis = visible_count();
    draw_game_header();
    draw_tabs();

    txt(2, 11, C_DIM, BG, "SUMARU");
    txt(2, 13, C_SOFT, BG, "City");
    txt(2, 15, C_DIM, BG, "Persona 2");
    txt(2, 16, C_DIM, BG, "Achievements");

    for (row = 0; row < vis && row < 6; ++row) {
        draw_achievement_row(row, visible_to_real(row));
    }

    if (vis == 0) txt(22, 15, C_DIM, BG, "No achievements in this filter.");
}

static void draw_game(void) {
    char b[64];
    draw_game_header();
    txt(4, 11, C_WHITE, BG_PANEL, "Game session");
    snprintf(b, sizeof(b), "Memory trigger  0x00001000   %02d / 10", progress);
    txt(4, 13, C_TEXT, BG_PANEL, b);
    txt(4, 15, C_SOFT, BG_PANEL, "X: simulate gameplay progress");
    txt(4, 17, C_SOFT, BG_PANEL, "TRIANGLE: toggle network");
    txt(4, 19, unlocked ? C_GREEN : C_DIM, BG_PANEL,
        unlocked ? "The Rumor Never Dies: unlocked" : "The Rumor Never Dies: locked");
}

static void draw_profile(void) {
    draw_game_header();
    txt(6, 11, C_WHITE, BG_PANEL, "RetroAchievements Profile");
    txt(6, 13, C_TEXT, BG_PANEL, "User            Tatsuya");
    txt(6, 15, C_TEXT, BG_PANEL, "Level           28");
    txt(6, 17, C_TEXT, BG_PANEL, online ? "Connection      Online" : "Connection      Offline");
    txt(6, 19, C_TEXT, BG_PANEL, queued ? "Pending         1 unlock" : "Pending         None");
}

static void draw_settings(void) {
    const char *opts[] = {"Overlay", "Audio", "Network", "Performance"};
    int i;
    for (i = 0; i < 4; ++i) {
        char b[48];
        unsigned int bg = i == item ? BG_FOCUS : BG_PANEL;
        snprintf(b, sizeof(b), " %-18s %s ", opts[i], i == 2 ? (online ? "ONLINE" : "OFFLINE") : "ON");
        txt(8, 9 + i * 3, i == item ? C_WHITE : C_TEXT, bg, b);
    }
}

static void draw_popup(void) {
    if (popup_frames <= 0) return;
    txt(27, 3, C_WHITE, BG_POP, " Achievement Unlocked        ");
    txt(27, 4, C_ICE, BG_POP, " The Rumor Never Dies        ");
    txt(27, 5, C_SOFT, BG_POP, " Witnessed the True End.     ");
    txt(27, 6, (online || synced) ? C_GREEN : C_YELLOW, BG_POP,
        (online || synced) ? " SYNCED                 50 pts" : " SAVED LOCAL            50 pts");
}

static void draw_footer(void) {
    hline(28, C_DIM);
    txt(2, 29, C_WHITE, BG_TOP, " X Select    O Back    L/R Filter ");
    txt(40, 29, C_SOFT, BG_TOP, "UP/DOWN Navigate");
}

static void render(void) {
    cls();
    draw_top();
    if (section == 0) draw_game();
    else if (section == 1) draw_achievements();
    else if (section == 2) draw_profile();
    else draw_settings();
    draw_popup();
    draw_footer();
}

static void evaluate(void) {
    if (!unlocked && progress >= 10) {
        unlocked = 1;
        popup_frames = 240;
        dirty = 1;
        log_line("UNLOCK achievement=1003 title=The_Rumor_Never_Dies");
        if (online) {
            synced = 1;
            log_line("SYNC achievement=1003 status=confirmed");
        } else {
            queued = 1;
            log_line("QUEUE achievement=1003 state=pending");
        }
    }

    if (online && queued && !synced) {
        synced = 1;
        queued = 0;
        popup_frames = 180;
        dirty = 1;
        log_line("SYNC achievement=1003 status=confirmed_after_reconnect");
    }
}

static int section_item_count(void) {
    if (section == 1) return visible_count();
    if (section == 3) return 4;
    return 1;
}

static void render_if_needed(void) {
    if (!dirty) return;
    sceDisplayWaitVblankStart();
    render();
    dirty = 0;
}

int main(int argc, char *argv[]) {
    SceCtrlData pad, old;
    (void)argc; (void)argv;

    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    memset(&old, 0, sizeof(old));
    log_line("BOOT RA-PSP Native UI v0.5");
    render_if_needed();

    while (1) {
        unsigned int pressed;
        int count;
        sceCtrlReadBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~old.Buttons;

        if (pressed & PSP_CTRL_LEFT) { section = (section + 3) % 4; item = 0; dirty = 1; }
        if (pressed & PSP_CTRL_RIGHT) { section = (section + 1) % 4; item = 0; dirty = 1; }

        count = section_item_count();
        if (pressed & PSP_CTRL_UP) { if (count > 0) item = (item + count - 1) % count; dirty = 1; }
        if (pressed & PSP_CTRL_DOWN) { if (count > 0) item = (item + 1) % count; dirty = 1; }

        if (section == 1) {
            if (pressed & PSP_CTRL_LTRIGGER) { filter_mode = (filter_mode + 2) % 3; item = 0; dirty = 1; }
            if (pressed & PSP_CTRL_RTRIGGER) { filter_mode = (filter_mode + 1) % 3; item = 0; dirty = 1; }
        }

        if (pressed & PSP_CTRL_CROSS) {
            if (section == 0) {
                if (progress < 10) progress++;
                evaluate();
                dirty = 1;
            } else if (section == 1 && visible_count() > 0) {
                int idx = visible_to_real(item);
                char b[96];
                snprintf(b, sizeof(b), "UI achievement_selected id=%d", idx);
                log_line(b);
            } else if (section == 3 && item == 2) {
                online = !online;
                evaluate();
                dirty = 1;
            }
        }

        if (pressed & PSP_CTRL_TRIANGLE) {
            online = !online;
            evaluate();
            dirty = 1;
            log_line(online ? "NETWORK online" : "NETWORK offline");
        }

        if (pressed & PSP_CTRL_CIRCLE) {
            if (section != 0) { section = 0; item = 0; dirty = 1; }
            else break;
        }

        if (popup_frames > 0) {
            popup_frames--;
            if (popup_frames == 0) dirty = 1;
        }

        render_if_needed();
        old = pad;
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
