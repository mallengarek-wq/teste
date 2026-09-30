#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psprtc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP PPSSPP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define SCREEN_W 480
#define SCREEN_H 272
#define MENU_HOLD_US 400000u
#define PROBE_ADDR ((volatile uint32_t*)0x0863AC2C)

static volatile int g_running = 1;
static volatile int g_menu_open = 0;
static volatile int g_popup_frames = 0;
static uint64_t g_combo_started = 0;

typedef struct { uint8_t r,g,b; } RGB;
static const RGB C_BG={13,24,39}, C_PANEL={31,50,70}, C_PANEL2={49,75,103};
static const RGB C_ACCENT={126,204,255}, C_WHITE={246,250,255}, C_SOFT={181,201,220};
static const RGB C_DIM={104,126,147}, C_GREEN={146,232,184}, C_YELLOW={255,219,124};

static const unsigned char font_alpha[26][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
static const unsigned char font_digit[10][7]={
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},
{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};

static unsigned char glyph_row(char ch,int row){
    unsigned char c=(unsigned char)ch;
    if(c>='a'&&c<='z') c=(unsigned char)(c-'a'+'A');
    if(c>='A'&&c<='Z') return font_alpha[c-'A'][row];
    if(c>='0'&&c<='9') return font_digit[c-'0'][row];
    switch(c){
        case ' ': return 0; case '.': return row==6?4:0; case ':': return (row==2||row==5)?4:0;
        case '-': return row==3?14:0; case '/': return row==0?1:row==1?2:row==2?2:row==3?4:row==4?8:row==5?8:16;
        case '+': return row==3?14:((row==2||row==4)?4:0); case '[': return (row==0||row==6)?14:8;
        case ']': return (row==0||row==6)?14:2; case '!': return row<5?4:(row==6?4:0);
        case '?': return row==0?14:row==1?17:row==2?2:row==3?4:row==5?4:0;
        default: return row==0||row==6?14:((row==1||row==5)?17:0);
    }
}

static uint32_t abgr(RGB c){return 0xFF000000u|((uint32_t)c.b<<16)|((uint32_t)c.g<<8)|c.r;}
static uint16_t pack16(RGB c,int fmt){
    if(fmt==PSP_DISPLAY_PIXEL_FORMAT_565) return (uint16_t)(((c.r>>3)<<11)|((c.g>>2)<<5)|(c.b>>3));
    if(fmt==PSP_DISPLAY_PIXEL_FORMAT_5551) return (uint16_t)(0x8000|((c.r>>3)<<10)|((c.g>>3)<<5)|(c.b>>3));
    return (uint16_t)(0xF000|((c.b>>4)<<8)|((c.g>>4)<<4)|(c.r>>4));
}
static void putpx(void*fb,int stride,int fmt,int x,int y,RGB c){
    if(x<0||x>=SCREEN_W||y<0||y>=SCREEN_H)return;
    if(fmt==PSP_DISPLAY_PIXEL_FORMAT_8888)((uint32_t*)fb)[y*stride+x]=abgr(c);
    else ((uint16_t*)fb)[y*stride+x]=pack16(c,fmt);
}
static void fill(void*fb,int stride,int fmt,int x0,int y0,int x1,int y1,RGB c){
    int x,y;if(x0<0)x0=0;if(y0<0)y0=0;if(x1>SCREEN_W)x1=SCREEN_W;if(y1>SCREEN_H)y1=SCREEN_H;
    for(y=y0;y<y1;y++)for(x=x0;x<x1;x++)putpx(fb,stride,fmt,x,y,c);
}
static void text(void*fb,int stride,int fmt,int x,int y,RGB c,const char*s,int scale){
    int i,row,col;if(!s)return;
    for(i=0;s[i]&&x<474;i++,x+=6*scale){
        for(row=0;row<7;row++){
            unsigned char bits=glyph_row(s[i],row);
            for(col=0;col<5;col++)if(bits&(1<<(4-col))){int xx,yy;for(yy=0;yy<scale;yy++)for(xx=0;xx<scale;xx++)putpx(fb,stride,fmt,x+col*scale+xx,y+row*scale+yy,c);}
        }
    }
}
static void box(void*fb,int stride,int fmt,int x0,int y0,int x1,int y1,RGB bg,RGB line){
    fill(fb,stride,fmt,x0,y0,x1,y1,bg);fill(fb,stride,fmt,x0,y0,x1,y0+1,line);fill(fb,stride,fmt,x0,y1-1,x1,y1,line);fill(fb,stride,fmt,x0,y0,x0+1,y1,line);fill(fb,stride,fmt,x1-1,y0,x1,y1,line);
}
static void draw_clock(void*fb,int stride,int fmt){
    pspTime t;char b[24];
    if(sceRtcGetCurrentClockLocalTime(&t)>=0)snprintf(b,sizeof(b),"%02d:%02d",t.hour,t.minutes);else strcpy(b,"--:--");
    text(fb,stride,fmt,421,12,C_SOFT,b,1);
}
static void draw_menu(void){
    void*fb=0;int stride=0,fmt=0;char b[96];uint32_t probe=0;
    if(sceDisplayGetFrameBuf(&fb,&stride,&fmt,PSP_DISPLAY_SETBUF_IMMEDIATE)<0||!fb)return;
    if(fmt!=PSP_DISPLAY_PIXEL_FORMAT_8888&&fmt!=PSP_DISPLAY_PIXEL_FORMAT_565&&fmt!=PSP_DISPLAY_PIXEL_FORMAT_5551&&fmt!=PSP_DISPLAY_PIXEL_FORMAT_4444)return;
    probe=*PROBE_ADDR;
    box(fb,stride,fmt,18,18,462,254,C_BG,C_ACCENT);
    fill(fb,stride,fmt,19,19,461,47,C_PANEL);
    text(fb,stride,fmt,30,27,C_WHITE,"RA-PSP",2); draw_clock(fb,stride,fmt);
    text(fb,stride,fmt,30,58,C_WHITE,"PERSONA 2: INNOCENT SIN",1);
    text(fb,stride,fmt,30,72,C_SOFT,"ULUS10584  /  PPSSPP USER-MODE",1);
    text(fb,stride,fmt,350,58,C_GREEN,"PLUGIN LOADED",1);

    box(fb,stride,fmt,30,96,440,124,C_PANEL2,C_ACCENT);
    text(fb,stride,fmt,42,104,C_WHITE,"LIVE GAME MEMORY",1);
    snprintf(b,sizeof(b),"0x0863AC2C  =  0x%08X",(unsigned)probe);
    text(fb,stride,fmt,190,104,C_YELLOW,b,1);

    box(fb,stride,fmt,30,134,440,170,C_PANEL,C_DIM);
    text(fb,stride,fmt,42,143,C_WHITE,"RETROACHIEVEMENTS BRIDGE",1);
    text(fb,stride,fmt,42,157,C_SOFT,"OVERLAY READY - ONLINE CLIENT NEXT",1);

    box(fb,stride,fmt,30,180,440,218,C_PANEL,C_DIM);
    text(fb,stride,fmt,42,190,C_WHITE,"IN-GAME OVERLAY TEST",1);
    text(fb,stride,fmt,42,204,C_SOFT,"TRIANGLE = TEST POPUP   O = CLOSE",1);
    text(fb,stride,fmt,30,235,C_DIM,"L+R+SELECT OPENS THIS MENU",1);
}
static void draw_popup(void){
    void*fb=0;int stride=0,fmt=0;
    if(sceDisplayGetFrameBuf(&fb,&stride,&fmt,PSP_DISPLAY_SETBUF_IMMEDIATE)<0||!fb)return;
    box(fb,stride,fmt,144,24,462,82,C_PANEL,C_ACCENT);
    fill(fb,stride,fmt,154,34,190,70,C_PANEL2);
    text(fb,stride,fmt,166,45,C_ACCENT,"RA",1);
    text(fb,stride,fmt,202,35,C_WHITE,"ACHIEVEMENT POPUP TEST",1);
    text(fb,stride,fmt,202,50,C_SOFT,"OVERLAY IS RUNNING IN-GAME",1);
    text(fb,stride,fmt,202,65,C_GREEN,"PPSSPP USER-MODE OK",1);
}

static int worker(SceSize args,void*argp){
    SceCtrlData pad,prev;uint64_t now=0;memset(&prev,0,sizeof(prev));(void)args;(void)argp;
    sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    while(g_running){
        sceCtrlPeekBufferPositive(&pad,1);
        if((pad.Buttons&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT))==(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT)){
            sceRtcGetCurrentTick(&now);
            if(!g_combo_started)g_combo_started=now;
            else if(!g_menu_open && now-g_combo_started>=MENU_HOLD_US){g_menu_open=1;g_combo_started=0;}
        }else g_combo_started=0;
        if(g_menu_open){
            if((pad.Buttons&PSP_CTRL_CIRCLE)&&!(prev.Buttons&PSP_CTRL_CIRCLE))g_menu_open=0;
            if((pad.Buttons&PSP_CTRL_TRIANGLE)&&!(prev.Buttons&PSP_CTRL_TRIANGLE))g_popup_frames=180;
        }
        sceDisplayWaitVblankStart();
        if(g_menu_open)draw_menu();
        if(g_popup_frames>0){draw_popup();g_popup_frames--;}
        prev=pad;
    }
    sceKernelExitDeleteThread(0);return 0;
}

int module_start(SceSize args,void*argp){
    SceUID th=(void)args,(void)argp,0; /* silences old toolchains poorly */
    th=sceKernelCreateThread("RA-PSP PPSSPP",worker,0x18,0x4000,PSP_THREAD_ATTR_USER,0);
    if(th>=0)sceKernelStartThread(th,0,0);
    return 0;
}
int module_stop(SceSize args,void*argp){(void)args;(void)argp;g_running=0;return 0;}
