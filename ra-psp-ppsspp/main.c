#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <stdint.h>

PSP_MODULE_INFO("RA-PSP PPSSPP", PSP_MODULE_USER, 1, 5);
PSP_NO_CREATE_MAIN_THREAD();

#define LOG_PATH "ms0:/PSP/PLUGINS/RA-PSP/ra_psp_ppsspp.log"
#define COMBO_MAIN (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT)
#define COMBO_FALLBACK (PSP_CTRL_START | PSP_CTRL_SELECT)
#define W 480
#define H 272

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

typedef struct { uint8_t r, g, b; } RGB;

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
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            put_px(fb, stride, fmt, x, y, c);
}

static void draw_text(void *fb, int stride, int fmt, int x, int y, RGB color, const char *text, int scale) {
    int i, row, col, xx, yy;
    for (i = 0; text && text[i] && x < 470; i++, x += 6 * scale) {
        for (row = 0; row < 7; row++) {
            unsigned char bits = glyph_row(text[i], row);
            for (col = 0; col < 5; col++) {
                if (bits & (1 << (4 - col))) {
                    for (yy = 0; yy < scale; yy++)
                        for (xx = 0; xx < scale; xx++)
                            put_px(fb, stride, fmt, x + col * scale + xx, y + row * scale + yy, color);
                }
            }
        }
    }
}

static int get_framebuffer(void **fb, int *stride, int *fmt) {
    *fb = 0;
    *stride = 0;
    *fmt = 0;
    if (sceDisplayGetFrameBuf(fb, stride, fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0) return 0;
    if (!*fb || *stride < 480 || *stride > 1024) return 0;
    if (*fmt != PSP_DISPLAY_PIXEL_FORMAT_8888 && *fmt != PSP_DISPLAY_PIXEL_FORMAT_565 &&
        *fmt != PSP_DISPLAY_PIXEL_FORMAT_5551 && *fmt != PSP_DISPLAY_PIXEL_FORMAT_4444) return 0;
    return 1;
}

static void draw_menu(void *fb, int stride, int fmt, int tab) {
    RGB bg = {45, 30, 27};
    RGB top = {81, 52, 32};
    RGB edge = {225, 184, 104};
    RGB white = {250, 245, 228};
    RGB soft = {199, 180, 155};
    RGB green = {163, 224, 166};
    RGB gold = {240, 194, 105};

    fill_rect(fb, stride, fmt, 24, 26, 456, 224, bg);
    fill_rect(fb, stride, fmt, 24, 26, 456, 58, top);
    fill_rect(fb, stride, fmt, 24, 26, 456, 28, edge);
    fill_rect(fb, stride, fmt, 24, 222, 456, 224, edge);
    fill_rect(fb, stride, fmt, 24, 26, 26, 224, edge);
    fill_rect(fb, stride, fmt, 454, 26, 456, 224, edge);

    draw_text(fb, stride, fmt, 38, 36, white, "RA-PSP", 2);
    draw_text(fb, stride, fmt, 38, 68, white, "PERSONA 2 INNOCENT SIN", 1);
    draw_text(fb, stride, fmt, 38, 84, soft, "PPSSPP LAZY OVERLAY V0.5", 1);

    draw_text(fb, stride, fmt, 38, 112, tab == 0 ? gold : soft, tab == 0 ? "> SUMMARY" : "  SUMMARY", 1);
    draw_text(fb, stride, fmt, 38, 130, tab == 1 ? gold : soft, tab == 1 ? "> ACHIEVEMENTS" : "  ACHIEVEMENTS", 1);
    draw_text(fb, stride, fmt, 38, 148, tab == 2 ? gold : soft, tab == 2 ? "> STATUS" : "  STATUS", 1);

    if (tab == 0) {
        draw_text(fb, stride, fmt, 210, 112, white, "PLUGIN", 1);
        draw_text(fb, stride, fmt, 350, 112, green, "OK", 1);
        draw_text(fb, stride, fmt, 210, 130, white, "INPUT", 1);
        draw_text(fb, stride, fmt, 350, 130, green, "OK", 1);
        draw_text(fb, stride, fmt, 210, 148, white, "RA ONLINE", 1);
        draw_text(fb, stride, fmt, 350, 148, soft, "OFF", 1);
    } else if (tab == 1) {
        draw_text(fb, stride, fmt, 210, 112, soft, "NO FAKE DATA", 1);
        draw_text(fb, stride, fmt, 210, 130, soft, "RC CLIENT NEXT", 1);
    } else {
        draw_text(fb, stride, fmt, 210, 112, green, "BOOT SAFE", 1);
        draw_text(fb, stride, fmt, 210, 130, soft, "DISPLAY ONLY AFTER HOTKEY", 1);
    }

    draw_text(fb, stride, fmt, 38, 196, soft, "LEFT RIGHT TABS   O CLOSE", 1);
}

static int worker(SceSize args, void *argp) {
    SceCtrlData pad;
    unsigned int previous = 0;
    int main_latched = 0;
    int fallback_latched = 0;
    int menu_open = 0;
    int tab = 0;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.5: worker started");

    for (;;) {
        pad.Buttons = 0;
        pad.Lx = 128;
        pad.Ly = 128;

        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            unsigned int buttons = pad.Buttons;

            if ((buttons & COMBO_MAIN) == COMBO_MAIN) {
                if (!main_latched) {
                    main_latched = 1;
                    menu_open = !menu_open;
                    write_line(menu_open ? "MENU OPEN: L+R+SELECT" : "MENU CLOSED: L+R+SELECT");
                }
            } else main_latched = 0;

            if ((buttons & COMBO_FALLBACK) == COMBO_FALLBACK) {
                if (!fallback_latched) {
                    fallback_latched = 1;
                    menu_open = !menu_open;
                    write_line(menu_open ? "MENU OPEN: START+SELECT" : "MENU CLOSED: START+SELECT");
                }
            } else fallback_latched = 0;

            if (menu_open) {
                if ((buttons & PSP_CTRL_CIRCLE) && !(previous & PSP_CTRL_CIRCLE)) {
                    menu_open = 0;
                    write_line("MENU CLOSED: CIRCLE");
                }
                if ((buttons & PSP_CTRL_LEFT) && !(previous & PSP_CTRL_LEFT)) {
                    tab--;
                    if (tab < 0) tab = 2;
                }
                if ((buttons & PSP_CTRL_RIGHT) && !(previous & PSP_CTRL_RIGHT)) {
                    tab++;
                    if (tab > 2) tab = 0;
                }
            }

            previous = buttons;
        }

        if (!menu_open) {
            sceKernelDelayThread(16667);
            continue;
        }

        {
            void *fb = 0;
            int stride = 0;
            int fmt = 0;
            sceDisplayWaitVblankStart();
            if (get_framebuffer(&fb, &stride, &fmt))
                draw_menu(fb, stride, fmt, tab);
            sceKernelDelayThread(1000);
        }
    }

    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID th;
    (void)args;
    (void)argp;

    write_line("RA-PSP v0.5: module_start");
    th = sceKernelCreateThread("RA-PSP PPSSPP Worker", worker, 0x40, 0x5000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) {
        write_line("ERROR: sceKernelCreateThread failed");
        return 0;
    }
    if (sceKernelStartThread(th, 0, 0) < 0)
        write_line("ERROR: sceKernelStartThread failed");
    else
        write_line("RA-PSP v0.5: worker launched");
    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    write_line("RA-PSP v0.5: module_stop");
    return 0;
}
