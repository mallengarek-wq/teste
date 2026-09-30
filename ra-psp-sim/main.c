#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

PSP_MODULE_INFO("RA-PSP Native UI", PSP_MODULE_USER, 1, 8);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define LOG_PATH "ms0:/PSP/RA_SIM_LOG.txt"
#define BUF_WIDTH 512
#define SCR_W 480
#define SCR_H 272
#define FB_SIZE (BUF_WIDTH*SCR_H*4)

#define COL_WHITE  0xFFFFFFFF
#define COL_SOFT   0xFFD9E8F6
#define COL_DIM    0xFF92A9BE
#define COL_BLUE   0xFFFFD99C
#define COL_GREEN  0xFFB6F0D3
#define COL_YELLOW 0xFF9DE4FF
#define COL_LOCK   0xFF6F8396
#define COL_ACCENT 0xFFFFC46D

static unsigned int __attribute__((aligned(16))) gu_list[262144];
static unsigned int __attribute__((aligned(16))) font_tex[128*64];
static void *fbp0 = 0;
static void *fbp1 = (void*)FB_SIZE;
static void *zbp  = (void*)(FB_SIZE*2);

static int progress=0, unlocked=0, queued=0, synced=0, online=0;
static int popup_frames=0, item=0, filter_mode=0, dirty=1;

static const char *ach_names[]={
    "Another Possibility","Friends, Again","The Rumor Never Dies",
    "City of Seven Sisters","Under the Same Moon","A Familiar Face"
};
static const char *ach_desc[]={
    "Cleared the game.","Reunited with all party members.","Witnessed the True End.",
    "Explored all areas of Sumaru City.","Viewed every Snow Queen rumor.","Spoke with the mysterious cat."
};
static const int ach_points[]={50,30,50,20,30,10};
static const int ach_static[]={1,1,0,1,0,1};

typedef struct { unsigned int color; short x,y,z; } Vtx;
typedef struct { unsigned short u,v; unsigned int color; short x,y,z; } TVtx;

static const unsigned char font_alpha[26][7] = {
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
static const unsigned char font_digit[10][7] = {
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},
{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};

static unsigned char glyph_row(char ch,int row){
    unsigned char c=(unsigned char)ch;
    if(c>='a'&&c<='z') c=(unsigned char)(c-'a'+'A');
    if(c>='A'&&c<='Z') return font_alpha[c-'A'][row];
    if(c>='0'&&c<='9') return font_digit[c-'0'][row];
    switch(c){
        case ' ': return 0;
        case '.': return row==6?4:0;
        case ',': return row==5?4:(row==6?8:0);
        case ':': return (row==2||row==5)?4:0;
        case ';': return row==2?4:(row==5?4:(row==6?8:0));
        case '-': return row==3?14:0;
        case '_': return row==6?31:0;
        case '/': return (row==0?1:row==1?2:row==2?2:row==3?4:row==4?8:row==5?8:16);
        case '\\': return (row==0?16:row==1?8:row==2?8:row==3?4:row==4?2:row==5?2:1);
        case '+': return row==3?14:((row==2||row==4)?4:0);
        case '%': return row==0?17:(row==1?2:(row==2?4:(row==3?4:(row==4?8:(row==5?16:17)))));
        case '[': return (row==0||row==6)?14:8;
        case ']': return (row==0||row==6)?14:2;
        case '(': return row==0?2:(row==1?4:(row==5?4:(row==6?2:8)));
        case ')': return row==0?8:(row==1?4:(row==5?4:(row==6?8:2)));
        case '<': return row==2?2:(row==3?4:(row==4?2:0));
        case '>': return row==2?8:(row==3?4:(row==4?8:0));
        case '!': return row<5?4:(row==6?4:0);
        case '?': return row==0?14:(row==1?17:(row==2?2:(row==3?4:(row==5?4:0))));
        case '=': return (row==2||row==4)?14:0;
        case '*': return row==2?21:(row==3?14:(row==4?21:0));
        default: return row==0||row==6?14:((row==1||row==5)?17:0);
    }
}

static void build_font_texture(void){
    int c,x,y;
    memset(font_tex,0,sizeof(font_tex));
    for(c=32;c<127;c++){
        int tx=(c&15)*8, ty=(c>>4)*8;
        for(y=0;y<7;y++){
            unsigned char bits=glyph_row((char)c,y);
            for(x=0;x<5;x++) if(bits&(1<<(4-x))) font_tex[(ty+y)*128+(tx+x)]=0xFFFFFFFF;
        }
    }
    sceKernelDcacheWritebackAll();
}

static void log_line(const char *s){
    SceUID fd=sceIoOpen(LOG_PATH,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
    if(fd>=0){sceIoWrite(fd,s,strlen(s));sceIoWrite(fd,"\n",1);sceIoClose(fd);}
}
static int ach_unlocked(int i){ if(i==0&&unlocked) return 1; return ach_static[i]; }
static int visible_count(void){ int i,n=0; for(i=0;i<6;i++){int u=ach_unlocked(i); if(filter_mode==0||(filter_mode==1&&u)||(filter_mode==2&&!u))n++;} return n; }
static int visible_to_real(int v){ int i,n=0; for(i=0;i<6;i++){int u=ach_unlocked(i); if(filter_mode==0||(filter_mode==1&&u)||(filter_mode==2&&!u)){if(n==v)return i;n++;}} return 0; }

static void rect(int x0,int y0,int x1,int y1,unsigned int color){
    Vtx *v=(Vtx*)sceGuGetMemory(2*sizeof(Vtx));
    sceGuDisable(GU_TEXTURE_2D);
    v[0].color=color; v[0].x=x0; v[0].y=y0; v[0].z=0;
    v[1].color=color; v[1].x=x1; v[1].y=y1; v[1].z=0;
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);
}
static void panel(int x0,int y0,int x1,int y1,unsigned int fill){
    rect(x0,y0,x1,y1,fill); rect(x0,y0,x1,y0+1,0x70DCEBFF); rect(x0,y1-1,x1,y1,0x305A7690);
}
static void glow_box(int x0,int y0,int x1,int y1){
    rect(x0-3,y0-3,x1+3,y1+3,0x204FA8E8); rect(x0-2,y0-2,x1+2,y1+2,0x405EB9F3);
    rect(x0-1,y0-1,x1+1,y1+1,0x7078C8FF); rect(x0,y0,x1,y1,0xB05283B6);
}

static void draw_string(const char *s,int x,int y,unsigned int color,int scale){
    int len=(int)strlen(s),i;
    TVtx *v;
    if(len<=0) return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexImage(0,128,64,128,font_tex);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST,GU_NEAREST);
    sceGuTexScale(1.0f/128.0f,1.0f/64.0f);
    sceGuTexOffset(0,0);
    v=(TVtx*)sceGuGetMemory(sizeof(TVtx)*2*len);
    for(i=0;i<len;i++){
        unsigned char c=(unsigned char)s[i]; int tx,ty,w=6*scale,h=8*scale;
        if(c<32||c>=127) c='?';
        tx=(c&15)*8; ty=(c>>4)*8;
        v[i*2].u=(unsigned short)tx; v[i*2].v=(unsigned short)ty; v[i*2].color=color; v[i*2].x=(short)x; v[i*2].y=(short)y; v[i*2].z=0;
        v[i*2+1].u=(unsigned short)(tx+6); v[i*2+1].v=(unsigned short)(ty+8); v[i*2+1].color=color; v[i*2+1].x=(short)(x+w); v[i*2+1].y=(short)(y+h); v[i*2+1].z=0;
        x+=w;
    }
    sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,len*2,0,v);
}
static void draw_centered(const char *s,int cx,int y,unsigned int color,int scale){ int w=(int)strlen(s)*6*scale; draw_string(s,cx-w/2,y,color,scale); }

static void draw_background(void){
    int y;
    for(y=0;y<SCR_H;y+=8){ unsigned int k=(unsigned int)(y/8); rect(0,y,SCR_W,y+8,0xFF172A3E + ((k&15)<<16)); }
    rect(0,0,SCR_W,30,0x70253F59); rect(0,246,SCR_W,272,0x80243A52);
    rect(0,29,SCR_W,30,0x9084B9D8); rect(0,245,SCR_W,246,0x7084B9D8);
    rect(0,145,100,245,0x24162939);
}
static void draw_header(void){
    int total=4+(unlocked?1:0), pct=(total*100)/6;
    panel(18,40,137,90,0x80324962); rect(22,44,133,86,0x90445B73); rect(22,44,133,47,0xA07FD2FF);
    panel(336,38,463,89,0x502A4058);
    draw_string("GAME",16,9,COL_SOFT,1); draw_string(online?"3/14 17:26 WIFI":"3/14 17:26 OFF",356,9,COL_SOFT,1);
    draw_string("PERSONA 2",26,54,COL_WHITE,1); draw_string("ETERNAL PUNISHMENT",26,69,COL_DIM,1);
    draw_string("PERSONA 2: ETERNAL PUNISHMENT",148,45,COL_WHITE,1);
    {
        char b[64]; snprintf(b,sizeof(b),"%d / 6 UNLOCKED",total); draw_string(b,148,64,COL_SOFT,1);
        panel(252,64,323,72,0x5034485C); rect(254,66,254+(pct*67)/100,70,0xE08DDCFF);
        snprintf(b,sizeof(b),"%d%%",pct); draw_string(b,327,64,COL_SOFT,1);
    }
    draw_string("TATSUYA",365,51,COL_SOFT,1); draw_string("LV. 28",365,67,COL_DIM,1);
}
static void draw_tabs(void){
    panel(140,94,464,116,0x50314960);
    if(filter_mode==0) glow_box(141,95,248,115);
    if(filter_mode==1) glow_box(249,95,356,115);
    if(filter_mode==2) glow_box(357,95,463,115);
    draw_centered("ALL",194,101,filter_mode==0?COL_WHITE:COL_DIM,1);
    draw_centered("UNLOCKED",302,101,filter_mode==1?COL_WHITE:COL_DIM,1);
    draw_centered("LOCKED",410,101,filter_mode==2?COL_WHITE:COL_DIM,1);
}
static void draw_sidebar(void){
    draw_string("SUMARU",18,136,COL_DIM,1); draw_string("CITY",18,155,COL_SOFT,1);
    draw_string("PERSONA 2",18,184,COL_DIM,1); draw_string("ACHIEVEMENTS",18,199,COL_DIM,1);
}
static void draw_list(void){
    int r,vis=visible_count();
    for(r=0;r<vis&&r<6;r++){
        int idx=visible_to_real(r),u=ach_unlocked(idx),y=122+r*20; char b[64];
        if(r==item) glow_box(104,y,465,y+18); else panel(104,y,465,y+18,u?0x462B4259:0x3026384B);
        rect(111,y+2,133,y+16,u?0xB0578AA8:0x60344150);
        if(u){ rect(428,y+5,437,y+14,0xD08DE2FF); rect(431,y+2,434,y+17,0x908DE2FF); }
        else { rect(429,y+7,436,y+15,0x806E8195); rect(431,y+4,434,y+9,0x806E8195); }
        draw_string(ach_names[idx],145,y+2,(r==item)?COL_WHITE:(u?COL_SOFT:COL_LOCK),1);
        snprintf(b,sizeof(b),"%d PTS",ach_points[idx]); draw_string(b,395,y+2,(r==item)?COL_WHITE:COL_DIM,1);
        draw_string(ach_desc[idx],145,y+10,(r==item)?COL_BLUE:(u?COL_DIM:COL_LOCK),1);
    }
}
static void draw_footer(void){
    draw_string("X SELECT",18,254,COL_WHITE,1); draw_string("O BACK",110,254,COL_SOFT,1);
    draw_string("L/R FILTER",198,254,COL_SOFT,1); draw_string("UP/DOWN NAVIGATE",320,254,COL_SOFT,1);
}
static void draw_popup(void){
    if(popup_frames<=0) return;
    glow_box(214,34,468,80); rect(222,42,254,72,0xB06C9CC5); rect(229,49,247,66,0xD08ED9FF);
    draw_string("ACHIEVEMENT UNLOCKED",266,43,COL_WHITE,1); draw_string("FIRST CONTACT",266,56,COL_SOFT,1);
    draw_string((online||synced)?"SYNCED +5":"SAVED LOCAL +5",266,67,(online||synced)?COL_GREEN:COL_YELLOW,1);
}
static void render(void){
    sceGuStart(GU_DIRECT,gu_list);
    sceGuClearColor(0xFF17283A); sceGuClearDepth(0); sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0); sceGuDisable(GU_DEPTH_TEST);
    draw_background(); draw_header(); draw_tabs(); draw_sidebar(); draw_list(); draw_footer(); draw_popup();
    sceGuFinish(); sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE); sceDisplayWaitVblankStart(); sceGuSwapBuffers();
}

static void evaluate(void){
    if(!unlocked&&progress>=10){unlocked=1;popup_frames=220;dirty=1;log_line("UNLOCK achievement=1001 title=First_Contact");if(online){synced=1;log_line("SYNC achievement=1001 status=confirmed");}else{queued=1;log_line("QUEUE achievement=1001 state=pending");}}
    if(online&&queued&&!synced){synced=1;queued=0;popup_frames=180;dirty=1;log_line("SYNC achievement=1001 status=confirmed_after_reconnect");}
}
static void init_graphics(void){
    build_font_texture(); sceGuInit(); sceGuStart(GU_DIRECT,gu_list);
    sceGuDrawBuffer(GU_PSM_8888,fbp0,BUF_WIDTH); sceGuDispBuffer(SCR_W,SCR_H,fbp1,BUF_WIDTH); sceGuDepthBuffer(zbp,BUF_WIDTH);
    sceGuOffset(2048-(SCR_W/2),2048-(SCR_H/2)); sceGuViewport(2048,2048,SCR_W,SCR_H); sceGuDepthRange(65535,0);
    sceGuScissor(0,0,SCR_W,SCR_H); sceGuEnable(GU_SCISSOR_TEST); sceGuDisable(GU_DEPTH_TEST); sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuFinish(); sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE); sceDisplayWaitVblankStart(); sceGuDisplay(GU_TRUE);
}

int main(int argc,char *argv[]){
    SceCtrlData pad,old; (void)argc;(void)argv;
    init_graphics(); sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG); memset(&old,0,sizeof(old));
    log_line("BOOT RA-PSP Native UI v0.8 unified-GU renderer"); render(); dirty=0;
    while(1){
        unsigned int pressed; int count=visible_count();
        sceCtrlReadBufferPositive(&pad,1); pressed=pad.Buttons & ~old.Buttons;
        if(pressed&PSP_CTRL_UP){if(count>0)item=(item+count-1)%count;dirty=1;}
        if(pressed&PSP_CTRL_DOWN){if(count>0)item=(item+1)%count;dirty=1;}
        if(pressed&PSP_CTRL_LTRIGGER){filter_mode=(filter_mode+2)%3;item=0;dirty=1;}
        if(pressed&PSP_CTRL_RTRIGGER){filter_mode=(filter_mode+1)%3;item=0;dirty=1;}
        if(pressed&PSP_CTRL_CROSS){if(progress<10)progress++;evaluate();dirty=1;}
        if(pressed&PSP_CTRL_TRIANGLE){online=!online;evaluate();dirty=1;log_line(online?"NETWORK online":"NETWORK offline");}
        if(pressed&PSP_CTRL_CIRCLE) break;
        if(popup_frames>0){popup_frames--;if(popup_frames==0)dirty=1;}
        if(dirty){render();dirty=0;}else sceDisplayWaitVblankStart();
        old=pad;
    }
    sceGuDisplay(GU_FALSE); sceGuTerm(); sceKernelExitGame(); return 0;
}
