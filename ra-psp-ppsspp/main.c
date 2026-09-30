#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <psprtc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP PPSSPP", PSP_MODULE_USER, 1, 6);
PSP_NO_CREATE_MAIN_THREAD();

#define W 480
#define H 272
#define LOG_PATH "ms0:/PSP/PLUGINS/RA-PSP/ra_psp_ppsspp.log"
#define COMBO_MAIN (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT)
#define COMBO_FALLBACK (PSP_CTRL_START | PSP_CTRL_SELECT)
#define TAB_SUMMARY 0
#define TAB_ACHIEVEMENTS 1
#define TAB_STATUS 2
#define MENU_X0 14
#define MENU_Y0 10
#define MENU_X1 466
#define MENU_Y1 258

typedef struct { uint8_t r, g, b; } RGB;

static const unsigned char FONT_ALPHA[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};

static const unsigned char FONT_DIGIT[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

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

static RGB rgb(uint8_t r, uint8_t g, uint8_t b) {
    RGB c;
    c.r = r; c.g = g; c.b = b;
    return c;
}

static unsigned char glyph_row(char ch, int row) {
    unsigned char c = (unsigned char)ch;
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return FONT_ALPHA[c - 'A'][row];
    if (c >= '0' && c <= '9') return FONT_DIGIT[c - '0'][row];
    switch (c) {
        case ' ': return 0;
        case '.': return row == 6 ? 4 : 0;
        case ':': return (row == 2 || row == 5) ? 4 : 0;
        case '-': return row == 3 ? 14 : 0;
        case '/': return row == 0 ? 1 : row == 1 ? 2 : row == 2 ? 2 : row == 3 ? 4 : row == 4 ? 8 : row == 5 ? 8 : 16;
        case '+': return row == 3 ? 14 : ((row == 2 || row == 4) ? 4 : 0);
        case '[': return (row == 0 || row == 6) ? 14 : 8;
        case ']': return (row == 0 || row == 6) ? 14 : 2;
        case '>': return row == 1 ? 8 : row == 2 ? 4 : row == 3 ? 2 : row == 4 ? 4 : row == 5 ? 8 : 0;
        case '<': return row == 1 ? 2 : row == 2 ? 4 : row == 3 ? 8 : row == 4 ? 4 : row == 5 ? 2 : 0;
        case '!': return row < 5 ? 4 : (row == 6 ? 4 : 0);
        default: return (row == 0 || row == 6) ? 14 : ((row == 1 || row == 5) ? 17 : 0);
    }
}

static uint32_t pack32(RGB c) {
    return 0xFF000000u | ((uint32_t)c.b << 16) | ((uint32_t)c.g << 8) | c.r;
}

static uint16_t pack16(RGB c, int fmt) {
    if (fmt == PSP_DISPLAY_PIXEL_FORMAT_565)
        return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
    if (fmt == PSP_DISPLAY_PIXEL_FORMAT_5551)
        return (uint16_t)(0x8000 | ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3));
    return (uint16_t)(0xF000 | ((c.b >> 4) << 8) | ((c.g >> 4) << 4) | (c.r >> 4));
}

static int bytes_per_pixel(int fmt) {
    return fmt == PSP_DISPLAY_PIXEL_FORMAT_8888 ? 4 : 2;
}

static void put_px(void *fb, int stride, int fmt, int x, int y, RGB c) {
    if (!fb || x < 0 || x >= W || y < 0 || y >= H) return;
    if (fmt == PSP_DISPLAY_PIXEL_FORMAT_8888)
        ((uint32_t *)fb)[y * stride + x] = pack32(c);
    else
        ((uint16_t *)fb)[y * stride + x] = pack16(c, fmt);
}

static void fill_rect(void *fb, int stride, int fmt, int x0, int y0, int x1, int y1, RGB c) {
    int x, y;
    if (!fb) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x1 <= x0 || y1 <= y0) return;

    if (fmt == PSP_DISPLAY_PIXEL_FORMAT_8888) {
        uint32_t value = pack32(c);
        for (y = y0; y < y1; y++) {
            uint32_t *row = (uint32_t *)fb + y * stride + x0;
            for (x = x0; x < x1; x++) *row++ = value;
        }
    } else {
        uint16_t value = pack16(c, fmt);
        for (y = y0; y < y1; y++) {
            uint16_t *row = (uint16_t *)fb + y * stride + x0;
            for (x = x0; x < x1; x++) *row++ = value;
        }
    }
}

static void outline_box(void *fb, int stride, int fmt, int x0, int y0, int x1, int y1, RGB bg, RGB edge) {
    fill_rect(fb, stride, fmt, x0, y0, x1, y1, bg);
    fill_rect(fb, stride, fmt, x0, y0, x1, y0 + 1, edge);
    fill_rect(fb, stride, fmt, x0, y1 - 1, x1, y1, edge);
    fill_rect(fb, stride, fmt, x0, y0, x0 + 1, y1, edge);
    fill_rect(fb, stride, fmt, x1 - 1, y0, x1, y1, edge);
}

static void draw_text(void *fb, int stride, int fmt, int x, int y, RGB c, const char *text, int scale) {
    int i, row, col, xx, yy;
    if (!text) return;
    for (i = 0; text[i] && x < 474; i++, x += 6 * scale) {
        for (row = 0; row < 7; row++) {
            unsigned char bits = glyph_row(text[i], row);
            for (col = 0; col < 5; col++) {
                if (bits & (1 << (4 - col))) {
                    for (yy = 0; yy < scale; yy++)
                        for (xx = 0; xx < scale; xx++)
                            put_px(fb, stride, fmt, x + col * scale + xx, y + row * scale + yy, c);
                }
            }
        }
    }
}

static int get_framebuffer(void **fb, int *stride, int *fmt) {
    int result;
    *fb = 0;
    *stride = 0;
    *fmt = 0;
    result = sceDisplayGetFrameBuf(fb, stride, fmt, PSP_DISPLAY_SETBUF_IMMEDIATE);
    if (result < 0 || !*fb || *stride < W || *stride > 1024) return 0;
    if (*fmt != PSP_DISPLAY_PIXEL_FORMAT_8888 &&
        *fmt != PSP_DISPLAY_PIXEL_FORMAT_565 &&
        *fmt != PSP_DISPLAY_PIXEL_FORMAT_5551 &&
        *fmt != PSP_DISPLAY_PIXEL_FORMAT_4444) return 0;
    return 1;
}

static void draw_clock(void *fb, int stride, int fmt, RGB color) {
    pspTime t;
    char buf[16];
    if (sceRtcGetCurrentClockLocalTime(&t) >= 0)
        snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minutes);
    else
        strcpy(buf, "--:--");
    draw_text(fb, stride, fmt, 419, 18, color, buf, 1);
}

static void draw_tab(void *fb, int stride, int fmt, int x0, int x1, const char *label, int active) {
    RGB bg = active ? rgb(183, 133, 78) : rgb(66, 45, 36);
    RGB edge = active ? rgb(236, 196, 118) : rgb(126, 93, 70);
    RGB textc = active ? rgb(255, 250, 230) : rgb(196, 176, 153);
    outline_box(fb, stride, fmt, x0, 72, x1, 100, bg, edge);
    draw_text(fb, stride, fmt, x0 + 10, 82, textc, label, 1);
}

static void draw_summary(void *fb, int stride, int fmt) {
    RGB white = rgb(250, 244, 226);
    RGB soft = rgb(198, 181, 158);
    RGB green = rgb(169, 222, 162);
    RGB gold = rgb(238, 194, 108);
    RGB panel = rgb(61, 42, 34);
    RGB edge = rgb(126, 93, 70);
    outline_box(fb, stride, fmt, 28, 110, 452, 209, panel, edge);
    draw_text(fb, stride, fmt, 42, 121, white, "PLUGIN ACTIVE", 1);
    draw_text(fb, stride, fmt, 328, 121, green, "OK", 1);
    draw_text(fb, stride, fmt, 42, 140, white, "HOTKEY AND INPUT", 1);
    draw_text(fb, stride, fmt, 328, 140, green, "OK", 1);
    draw_text(fb, stride, fmt, 42, 159, white, "RETROACHIEVEMENTS", 1);
    draw_text(fb, stride, fmt, 255, 159, gold, "NOT CONNECTED", 1);
    draw_text(fb, stride, fmt, 42, 180, soft, "NEXT: RC_CLIENT LOGIN + LIVE SET", 1);
    draw_text(fb, stride, fmt, 42, 194, soft, "NO FAKE ACHIEVEMENT DATA SHOWN", 1);
}

static void draw_achievements(void *fb, int stride, int fmt, int selected) {
    RGB white = rgb(250, 244, 226);
    RGB soft = rgb(198, 181, 158);
    RGB gold = rgb(238, 194, 108);
    RGB panel = rgb(61, 42, 34);
    RGB panel2 = rgb(86, 59, 44);
    RGB edge = rgb(126, 93, 70);
    int y0[3] = {112, 142, 172};
    int i;
    const char *labels[3] = {
        "NO ACHIEVEMENT SET LOADED",
        "ONLINE BRIDGE DISABLED IN V0.6",
        "REAL RA TITLES WILL APPEAR HERE"
    };
    for (i = 0; i < 3; i++) {
        RGB bg = (i == selected) ? panel2 : panel;
        RGB ec = (i == selected) ? gold : edge;
        outline_box(fb, stride, fmt, 28, y0[i], 452, y0[i] + 25, bg, ec);
        draw_text(fb, stride, fmt, 42, y0[i] + 9, (i == selected) ? white : soft, labels[i], 1);
    }
    draw_text(fb, stride, fmt, 42, 210, soft, "UP/DOWN PREVIEWS LIST NAVIGATION", 1);
}

static void draw_status(void *fb, int stride, int fmt) {
    RGB white = rgb(250, 244, 226);
    RGB soft = rgb(198, 181, 158);
    RGB green = rgb(169, 222, 162);
    RGB gold = rgb(238, 194, 108);
    RGB panel = rgb(61, 42, 34);
    RGB edge = rgb(126, 93, 70);
    outline_box(fb, stride, fmt, 28, 110, 452, 213, panel, edge);
    draw_text(fb, stride, fmt, 42, 121, white, "PLUGIN", 1);
    draw_text(fb, stride, fmt, 310, 121, green, "LOADED", 1);
    draw_text(fb, stride, fmt, 42, 140, white, "INPUT", 1);
    draw_text(fb, stride, fmt, 310, 140, green, "OK", 1);
    draw_text(fb, stride, fmt, 42, 159, white, "RENDERER", 1);
    draw_text(fb, stride, fmt, 244, 159, gold, "BUFFERED V0.6", 1);
    draw_text(fb, stride, fmt, 42, 178, white, "NETWORK / RCHEEVOS", 1);
    draw_text(fb, stride, fmt, 310, 178, soft, "OFF", 1);
    draw_text(fb, stride, fmt, 42, 197, soft, "GAME RAM IS NOT READ IN THIS BUILD", 1);
}

static void render_menu(void *surface, int fmt, int tab, int selected) {
    int stride = W;
    RGB bg = rgb(48, 31, 27);
    RGB top = rgb(81, 52, 32);
    RGB edge = rgb(213, 170, 100);
    RGB white = rgb(250, 244, 226);
    RGB soft = rgb(198, 181, 158);

    outline_box(surface, stride, fmt, MENU_X0, MENU_Y0, MENU_X1, MENU_Y1, bg, edge);
    fill_rect(surface, stride, fmt, 15, 11, 465, 58, top);
    draw_text(surface, stride, fmt, 27, 22, white, "RA-PSP", 2);
    draw_clock(surface, stride, fmt, soft);
    draw_text(surface, stride, fmt, 27, 48, white, "PERSONA 2: INNOCENT SIN", 1);
    draw_text(surface, stride, fmt, 278, 48, soft, "ULUS10584 / PPSSPP", 1);

    draw_tab(surface, stride, fmt, 22, 155, "SUMMARY", tab == TAB_SUMMARY);
    draw_tab(surface, stride, fmt, 159, 327, "ACHIEVEMENTS", tab == TAB_ACHIEVEMENTS);
    draw_tab(surface, stride, fmt, 331, 458, "STATUS", tab == TAB_STATUS);

    if (tab == TAB_SUMMARY) draw_summary(surface, stride, fmt);
    else if (tab == TAB_ACHIEVEMENTS) draw_achievements(surface, stride, fmt, selected);
    else draw_status(surface, stride, fmt);

    fill_rect(surface, stride, fmt, 15, 229, 465, 257, rgb(55, 36, 31));
    draw_text(surface, stride, fmt, 26, 239, soft, "LEFT/RIGHT TABS", 1);
    draw_text(surface, stride, fmt, 169, 239, soft, "TRIANGLE TEST POPUP", 1);
    draw_text(surface, stride, fmt, 363, 239, soft, "O CLOSE", 1);
}

static void blit_menu(void *surface, int fmt, void *fb, int fb_stride) {
    int y;
    int bpp = bytes_per_pixel(fmt);
    int width = MENU_X1 - MENU_X0;
    for (y = MENU_Y0; y < MENU_Y1; y++) {
        unsigned char *src = (unsigned char *)surface + (y * W + MENU_X0) * bpp;
        unsigned char *dst = (unsigned char *)fb + (y * fb_stride + MENU_X0) * bpp;
        memcpy(dst, src, width * bpp);
    }
}

static void draw_popup_direct(void *fb, int stride, int fmt) {
    RGB panel = rgb(81, 52, 32);
    RGB edge = rgb(238, 194, 108);
    RGB white = rgb(250, 244, 226);
    RGB soft = rgb(198, 181, 158);
    RGB green = rgb(169, 222, 162);
    outline_box(fb, stride, fmt, 176, 18, 466, 82, panel, edge);
    outline_box(fb, stride, fmt, 187, 29, 229, 71, rgb(104, 70, 47), edge);
    draw_text(fb, stride, fmt, 198, 45, edge, "RA", 1);
    draw_text(fb, stride, fmt, 240, 29, white, "TEST POPUP", 1);
    draw_text(fb, stride, fmt, 240, 45, soft, "OVERLAY PATH OK", 1);
    draw_text(fb, stride, fmt, 240, 61, green, "NO RA UNLOCK SENT", 1);
}

static int worker(SceSize args, void *argp) {
    SceCtrlData pad;
    unsigned int previous = 0;
    int combo_main_latched = 0;
    int combo_fallback_latched = 0;
    int menu_open = 0;
    int tab = TAB_SUMMARY;
    int selected = 0;
    int popup_frames = 0;
    int menu_dirty = 1;
    int rendered_fmt = -1;
    int clock_counter = 0;
    void *menu_surface = malloc(W * H * 4);
    (void)args;
    (void)argp;

    if (!menu_surface) {
        write_line("ERROR: overlay buffer allocation failed");
        return 0;
    }

    write_line("RA-PSP v0.6: worker started");

    for (;;) {
        void *fb = 0;
        int fb_stride = 0;
        int fb_fmt = 0;

        memset(&pad, 0, sizeof(pad));
        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            unsigned int buttons = pad.Buttons;

            if ((buttons & COMBO_MAIN) == COMBO_MAIN) {
                if (!combo_main_latched) {
                    combo_main_latched = 1;
                    menu_open = !menu_open;
                    menu_dirty = 1;
                    write_line(menu_open ? "MENU OPEN: L+R+SELECT" : "MENU CLOSED: L+R+SELECT");
                }
            } else combo_main_latched = 0;

            if ((buttons & COMBO_FALLBACK) == COMBO_FALLBACK) {
                if (!combo_fallback_latched) {
                    combo_fallback_latched = 1;
                    menu_open = !menu_open;
                    menu_dirty = 1;
                    write_line(menu_open ? "MENU OPEN: START+SELECT" : "MENU CLOSED: START+SELECT");
                }
            } else combo_fallback_latched = 0;

            if (menu_open) {
                if ((buttons & PSP_CTRL_CIRCLE) && !(previous & PSP_CTRL_CIRCLE)) {
                    menu_open = 0;
                    write_line("MENU CLOSED: CIRCLE");
                }
                if ((buttons & PSP_CTRL_LEFT) && !(previous & PSP_CTRL_LEFT)) {
                    tab--;
                    if (tab < TAB_SUMMARY) tab = TAB_STATUS;
                    menu_dirty = 1;
                }
                if ((buttons & PSP_CTRL_RIGHT) && !(previous & PSP_CTRL_RIGHT)) {
                    tab++;
                    if (tab > TAB_STATUS) tab = TAB_SUMMARY;
                    menu_dirty = 1;
                }
                if (tab == TAB_ACHIEVEMENTS) {
                    if ((buttons & PSP_CTRL_UP) && !(previous & PSP_CTRL_UP)) {
                        selected--;
                        if (selected < 0) selected = 2;
                        menu_dirty = 1;
                    }
                    if ((buttons & PSP_CTRL_DOWN) && !(previous & PSP_CTRL_DOWN)) {
                        selected++;
                        if (selected > 2) selected = 0;
                        menu_dirty = 1;
                    }
                }
                if ((buttons & PSP_CTRL_TRIANGLE) && !(previous & PSP_CTRL_TRIANGLE)) {
                    popup_frames = 150;
                    write_line("TEST POPUP TRIGGERED");
                }
            }
            previous = buttons;
        }

        sceDisplayWaitVblankStart();
        sceKernelDelayThread(1000);

        if (get_framebuffer(&fb, &fb_stride, &fb_fmt)) {
            if (menu_open) {
                clock_counter++;
                if (clock_counter >= 60) {
                    clock_counter = 0;
                    menu_dirty = 1;
                }
                if (rendered_fmt != fb_fmt) {
                    rendered_fmt = fb_fmt;
                    menu_dirty = 1;
                }
                if (menu_dirty) {
                    render_menu(menu_surface, fb_fmt, tab, selected);
                    menu_dirty = 0;
                }
                blit_menu(menu_surface, fb_fmt, fb, fb_stride);
            } else {
                clock_counter = 0;
            }

            if (popup_frames > 0) {
                draw_popup_direct(fb, fb_stride, fb_fmt);
                popup_frames--;
            }
        }
    }

    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th;
    (void)args;
    (void)argp;
    write_line("RA-PSP v0.6: module_start");
    th = sceKernelCreateThread("RA-PSP PPSSPP Worker", worker, 0x40, 0x6000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        write_line("ERROR: sceKernelCreateThread failed");
        return 0;
    }
    if (sceKernelStartThread(th, 0, 0) < 0)
        write_line("ERROR: sceKernelStartThread failed");
    else
        write_line("RA-PSP v0.6: worker launched");
    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    write_line("RA-PSP v0.6: module_stop");
    return 0;
}
