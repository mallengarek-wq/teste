#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP Native UI", PSP_MODULE_USER, 1, 7);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define LOG_PATH "ms0:/PSP/RA_SIM_LOG.txt"
#define BUF_WIDTH 512
#define SCR_W 480
#define SCR_H 272
#define FB_SIZE (BUF_WIDTH*SCR_H*4)

#define C_WHITE  0x00FFFFFF
#define C_SOFT   0x00D7E7F5
#define C_DIM    0x008FA6BC
#define C_BLUE   0x00F3D7A1
#define C_GREEN  0x009FE5C0
#define C_YELLOW 0x0078D9FF
#define C_LOCK   0x00798A9C

static unsigned int __attribute__((aligned(16))) gu_list[262144];
static void *fbp0 = 0;
static void *fbp1 = (void*)FB_SIZE;
static void *zbp  = (void*)(FB_SIZE*2);

static int progress=0, unlocked=0, queued=0, synced=0, online=0;
static int popup_frames=0, item=0, filter_mode=0, dirty=1;
static const char *ach_names[]={"Another Possibility","Friends, Again","The Rumor Never Dies","City of Seven Sisters","Under the Same Moon","A Familiar Face"};
static const char *ach_desc[]={"Cleared the game.","Reunited with all party members.","Witnessed the True End.","Explored all areas of Sumaru City.","Viewed every Snow Queen rumor.","Spoke with the mysterious cat."};
static const int ach_points[]={50,30,50,20,30,10};
static const int ach_static[]={1,1,0,1,0,1};

typedef struct { unsigned int color; short x,y,z; } Vtx;

static void log_line(const char *s){ SceUID fd=sceIoOpen(LOG_PATH,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777); if(fd>=0){sceIoWrite(fd,s,strlen(s));sceIoWrite(fd,"\n",1);sceIoClose(fd);} }
static int ach_unlocked(int i){ if(i==0 && unlocked) return 1; return ach_static[i]; }
static int visible_count(void){ int i,n=0; for(i=0;i<6;i++){int u=ach_unlocked(i); if(filter_mode==0||(filter_mode==1&&u)||(filter_mode==2&&!u))n++;} return n; }
static int visible_to_real(int v){ int i,n=0; for(i=0;i<6;i++){int u=ach_unlocked(i); if(filter_mode==0||(filter_mode==1&&u)||(filter_mode==2&&!u)){if(n==v)return i;n++;}} return 0; }

static void rect(int x0,int y0,int x1,int y1,unsigned int color){
    Vtx *v=(Vtx*)sceGuGetMemory(2*sizeof(Vtx));
    v[0].color=color; v[0].x=x0; v[0].y=y0; v[0].z=0;
    v[1].color=color; v[1].x=x1; v[1].y=y1; v[1].z=0;
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);
}

static void panel(int x0,int y0,int x1,int y1,unsigned int fill){
    rect(x0,y0,x1,y1,fill);
    rect(x0,y0,x1,y0+1,0x70DCEBFF);
    rect(x0,y1-1,x1,y1,0x305A7690);
}

static void glow_box(int x0,int y0,int x1,int y1){
    rect(x0-3,y0-3,x1+3,y1+3,0x204FA8E8);
    rect(x0-2,y0-2,x1+2,y1+2,0x405EB9F3);
    rect(x0-1,y0-1,x1+1,y1+1,0x7078C8FF);
    rect(x0,y0,x1,y1,0xB05283B6);
}

static void draw_background(void){
    int y;
    for(y=0;y<SCR_H;y+=8){
        unsigned int b=0xFF1A2C40 + ((unsigned int)(y/8)<<16);
        rect(0,y,SCR_W,y+8,b);
    }
    rect(0,0,SCR_W,32,0x50253E5B);
    rect(0,248,SCR_W,272,0x70243A52);
    rect(0,31,SCR_W,32,0x804E789A);
    rect(0,247,SCR_W,248,0x804E789A);
    rect(0,160,160,248,0x22192B3D);
    rect(335,33,480,95,0x30283E57);
}

static void draw_header_graphics(void){
    panel(18,42,145,92,0x80314963);
    rect(22,46,141,88,0x90435B72);
    rect(22,46,141,49,0xA07FD2FF);
    panel(335,39,465,91,0x502A4058);
    rect(213,70,315,78,0x6035485C);
    rect(215,72,215+(progress>10?10:progress)*9,76,0xD089D9FF);
}

static void draw_tabs_graphics(void){
    int x0=146;
    panel(x0,98,x0+318,119,0x50314960);
    if(filter_mode==0) glow_box(147,99,252,118);
    if(filter_mode==1) glow_box(253,99,358,118);
    if(filter_mode==2) glow_box(359,99,463,118);
}

static void draw_list_graphics(void){
    int r,vis=visible_count();
    for(r=0;r<vis && r<6;r++){
        int y=126+r*19;
        int idx=visible_to_real(r);
        int u=ach_unlocked(idx);
        if(r==item) glow_box(109,y,464,y+17);
        else panel(109,y,464,y+17,u?0x402A4159:0x2824384C);
        rect(116,y+2,140,y+15,u?0x905789A9:0x50323F4D);
        if(u){
            rect(425,y+5,434,y+13,0xC08BE0FF);
            rect(428,y+2,431,y+16,0x908BE0FF);
        }else{
            rect(426,y+6,433,y+14,0x806E8195);
            rect(428,y+3,431,y+8,0x806E8195);
        }
    }
}

static void draw_popup_graphics(void){
    if(popup_frames<=0) return;
    glow_box(214,39,468,82);
    rect(222,47,254,75,0xB06C9CC5);
    rect(229,54,247,68,0xD08ED9FF);
}

static void gu_begin(void){
    sceGuStart(GU_DIRECT,gu_list);
    sceGuClearColor(0xFF17283A);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuDisable(GU_DEPTH_TEST);
}

static void gu_end_and_text(void){
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
    pspDebugScreenSetOffset((int)fbp0);
    pspDebugScreenSetBase((void*)((unsigned int)0x44000000 + (unsigned int)fbp0));
    pspDebugScreenEnableBackColor(0);
}

static void txt(int x,int y,unsigned int c,const char *s){ pspDebugScreenSetXY(x,y); pspDebugScreenSetTextColor(c); pspDebugScreenPrintf("%s",s); }

static void draw_text(void){
    char b[96]; int r,vis=visible_count(); int total=4+(unlocked?1:0); int pct=(total*100)/6;
    txt(2,0,C_SOFT,"[GAME]");
    txt(49,0,C_SOFT,online?"3/14  17:26  WIFI":"3/14  17:26  OFF");
    txt(3,6,C_WHITE,"PERSONA 2");
    txt(3,8,C_DIM,"ETERNAL PUNISHMENT");
    txt(18,5,C_WHITE,"Persona 2: Eternal Punishment");
    snprintf(b,sizeof(b),"%d / 6 unlocked",total); txt(18,8,C_SOFT,b);
    snprintf(b,sizeof(b),"%d%%",pct); txt(40,8,C_SOFT,b);
    txt(43,5,C_SOFT,"[:)]  Tatsuya");
    txt(48,7,C_DIM,"Lv. 28");
    txt(20,13,filter_mode==0?C_WHITE:C_DIM,"All");
    txt(32,13,filter_mode==1?C_WHITE:C_DIM,"Unlocked");
    txt(47,13,filter_mode==2?C_WHITE:C_DIM,"Locked");
    txt(3,19,C_DIM,"SUMARU"); txt(3,22,C_SOFT,"City"); txt(3,26,C_DIM,"Persona 2"); txt(3,28,C_DIM,"Achievements");
    for(r=0;r<vis && r<6;r++){
        int idx=visible_to_real(r),u=ach_unlocked(idx), y=16+r*2;
        snprintf(b,sizeof(b),"%-26s %2d pts",ach_names[idx],ach_points[idx]);
        txt(18,y,(r==item)?C_WHITE:(u?C_SOFT:C_LOCK),b);
        snprintf(b,sizeof(b),"%-30s %s",ach_desc[idx],u?"OK":"LOCK");
        txt(18,y+1,(r==item)?C_BLUE:(u?C_DIM:C_LOCK),b);
    }
    txt(3,31,C_WHITE,"X Select      O Back      L/R Filter      UP/DOWN Navigate");
    if(popup_frames>0){
        txt(28,6,C_WHITE,"Achievement Unlocked");
        txt(28,7,C_SOFT,"First Contact");
        txt(28,8,(online||synced)?C_GREEN:C_YELLOW,(online||synced)?"SYNCED  +5":"SAVED LOCAL  +5");
    }
}

static void render(void){
    gu_begin();
    draw_background();
    draw_header_graphics();
    draw_tabs_graphics();
    draw_list_graphics();
    draw_popup_graphics();
    gu_end_and_text();
    draw_text();
    sceKernelDcacheWritebackAll();
    sceDisplayWaitVblankStart();
    fbp0=sceGuSwapBuffers();
}

static void evaluate(void){
    if(!unlocked && progress>=10){ unlocked=1; popup_frames=220; dirty=1; log_line("UNLOCK achievement=1001 title=First_Contact"); if(online){synced=1;log_line("SYNC achievement=1001 status=confirmed");}else{queued=1;log_line("QUEUE achievement=1001 state=pending");}}
    if(online&&queued&&!synced){synced=1;queued=0;popup_frames=180;dirty=1;log_line("SYNC achievement=1001 status=confirmed_after_reconnect");}
}

static void init_graphics(void){
    pspDebugScreenInitEx(fbp0, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
    pspDebugScreenEnableBackColor(0);
    sceGuInit();
    sceGuStart(GU_DIRECT,gu_list);
    sceGuDrawBuffer(GU_PSM_8888,fbp0,BUF_WIDTH);
    sceGuDispBuffer(SCR_W,SCR_H,fbp1,BUF_WIDTH);
    sceGuDepthBuffer(zbp,BUF_WIDTH);
    sceGuOffset(2048-(SCR_W/2),2048-(SCR_H/2));
    sceGuViewport(2048,2048,SCR_W,SCR_H);
    sceGuDepthRange(65535,0);
    sceGuScissor(0,0,SCR_W,SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(int argc,char *argv[]){
    SceCtrlData pad,old; (void)argc;(void)argv;
    init_graphics(); sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG); memset(&old,0,sizeof(old));
    log_line("BOOT RA-PSP Native UI v0.7 GU text-fix"); render(); dirty=0;
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
        if(popup_frames>0){popup_frames--; if(popup_frames==0)dirty=1;}
        if(dirty){render();dirty=0;} else sceDisplayWaitVblankStart();
        old=pad;
    }
    sceGuTerm(); sceKernelExitGame(); return 0;
}
