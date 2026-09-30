#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP XMB UI", PSP_MODULE_USER, 1, 4);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define LOG_PATH "ms0:/PSP/RA_SIM_LOG.txt"
#define C_WHITE   0x00F7F9FF
#define C_SOFT    0x00B8C5D0
#define C_DIM     0x00798694
#define C_BLUE    0x00F0C58E
#define C_GREEN   0x0098E8A8
#define C_YELLOW  0x0070D8F8
#define C_RED     0x007080F0
#define BG        0x00302014
#define BG_BAR    0x00513D25
#define BG_FOCUS  0x00886232
#define BG_POP    0x00604424

static int progress = 0, unlocked = 0, queued = 0, synced = 0, online = 0;
static int popup_frames = 0, dirty = 1;
static int section = 1; /* 0 game, 1 achievements, 2 profile, 3 settings */
static int item = 0;
static int filter_mode = 0;

static const char *sections[] = {"GAME", "ACHIEVEMENTS", "PROFILE", "SETTINGS"};
static const char *ach_names[] = {
    "First Contact", "Rumor Monger", "Friends, Again",
    "City of Seven Sisters", "Under the Same Moon", "Another Possibility"
};
static const char *ach_desc[] = {
    "Reach memory value 10.", "Trigger your first rumor.",
    "Reunite with all party members.", "Explore all Sumaru districts.",
    "View every Moon rumor.", "Clear the game."
};
static const int ach_points[] = {5,10,30,20,30,50};
static const int ach_static[] = {0,1,1,1,0,0};

static void log_line(const char *s) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, s, strlen(s)); sceIoWrite(fd, "\n", 1); sceIoClose(fd); }
}

static void cls(void) {
    pspDebugScreenSetBackColor(BG);
    pspDebugScreenSetTextColor(C_WHITE);
    pspDebugScreenClear();
}

static void txt(int x,int y,unsigned int fg,unsigned int bg,const char *s) {
    pspDebugScreenSetXY(x,y);
    pspDebugScreenSetTextColor(fg);
    pspDebugScreenSetBackColor(bg);
    pspDebugScreenPrintf("%s",s);
}

static int ach_unlocked(int i){ return i==0 ? unlocked : ach_static[i]; }

static int visible_count(void){
    int i,n=0;
    for(i=0;i<6;i++){
        int u=ach_unlocked(i);
        if(filter_mode==0 || (filter_mode==1&&u) || (filter_mode==2&&!u)) n++;
    }
    return n;
}

static int visible_to_real(int v){
    int i,n=0;
    for(i=0;i<6;i++){
        int u=ach_unlocked(i);
        if(filter_mode==0 || (filter_mode==1&&u) || (filter_mode==2&&!u)){
            if(n==v) return i;
            n++;
        }
    }
    return 0;
}

static void draw_top_status(void){
    txt(2,0,C_SOFT,BG,"RA-PSP");
    txt(46,0,online?C_GREEN:C_DIM,BG,online?"ONLINE":"OFFLINE");
    txt(0,1,C_DIM,BG,"------------------------------------------------------------");
}

static void draw_xmb_categories(void){
    int i,x=4;
    for(i=0;i<4;i++){
        unsigned int fg = (i==section)?C_WHITE:C_DIM;
        unsigned int bg = (i==section)?BG_FOCUS:BG;
        char b[20];
        snprintf(b,sizeof(b)," %s ",sections[i]);
        txt(x,3,fg,bg,b);
        x += (int)strlen(sections[i]) + 4;
    }
}

static void draw_footer(const char *extra){
    txt(0,29,C_DIM,BG,"------------------------------------------------------------");
    txt(2,30,C_SOFT,BG,"LEFT/RIGHT Category   UP/DOWN Select   X Enter   O Back");
    if(extra) txt(2,31,C_DIM,BG,extra);
}

static void draw_popup(void){
    if(popup_frames<=0) return;
    txt(24,5,C_BLUE,BG_POP,"  Achievement Unlocked                 ");
    txt(24,6,C_WHITE,BG_POP,"  [*] First Contact                    ");
    txt(24,7,C_SOFT,BG_POP,"      Reach memory value 10.           ");
    txt(24,8,(online||synced)?C_GREEN:C_YELLOW,BG_POP,
        (online||synced)?"      SYNCED                         ":"      SAVED LOCAL                    ");
}

static void draw_game(void){
    char b[64];
    txt(4,7,C_WHITE,BG,"UMD Game");
    txt(4,9,C_SOFT,BG,"Persona-like PSP Test");
    snprintf(b,sizeof(b),"Achievement progress: %02d / 10",progress);
    txt(4,12,C_WHITE,BG,b);
    txt(4,14,unlocked?C_GREEN:C_DIM,BG,unlocked?"First Contact  - unlocked":"First Contact  - locked");
    txt(4,17,C_DIM,BG,"X: simulate gameplay progress");
    txt(4,19,C_DIM,BG,"TRIANGLE: toggle network");
}

static void draw_achievements(void){
    char b[64]; int row,vis=visible_count();
    int total=3+(unlocked?1:0);
    snprintf(b,sizeof(b),"Persona-like PSP Test                %d / 6",total);
    txt(4,6,C_WHITE,BG,b);

    txt(4,8,filter_mode==0?C_WHITE:C_DIM,filter_mode==0?BG_FOCUS:BG," ALL ");
    txt(11,8,filter_mode==1?C_WHITE:C_DIM,filter_mode==1?BG_FOCUS:BG," UNLOCKED ");
    txt(23,8,filter_mode==2?C_WHITE:C_DIM,filter_mode==2?BG_FOCUS:BG," LOCKED ");

    for(row=0;row<vis && row<6;row++){
        int idx=visible_to_real(row), y=11+row*3, u=ach_unlocked(idx);
        unsigned int bg=(row==item)?BG_FOCUS:BG;
        unsigned int fg=(row==item)?C_WHITE:(u?C_SOFT:C_DIM);
        snprintf(b,sizeof(b)," %c  %-31s %3d pts ",u?'*':'o',ach_names[idx],ach_points[idx]);
        txt(4,y,fg,bg,b);
        snprintf(b,sizeof(b),"    %-44s",ach_desc[idx]);
        txt(4,y+1,row==item?C_BLUE:C_DIM,bg,b);
    }
    if(vis==0) txt(15,14,C_DIM,BG,"No achievements in this filter.");
}

static void draw_profile(void){
    txt(4,7,C_WHITE,BG,"RetroAchievements Profile");
    txt(4,10,C_SOFT,BG,"User              Not signed in");
    txt(4,12,C_SOFT,BG,"Session           Local test mode");
    txt(4,14,C_SOFT,BG,online?"Connection        Online":"Connection        Offline");
    txt(4,16,C_SOFT,BG,queued?"Pending unlocks   1":"Pending unlocks   0");
    txt(4,19,C_DIM,BG,"Real account data will come from rc_client.");
}

static void draw_settings(void){
    const char *opts[]={"Overlay","Audio","Network","Performance"};
    int i;
    for(i=0;i<4;i++){
        char b[48]; unsigned int bg=(i==item)?BG_FOCUS:BG;
        snprintf(b,sizeof(b)," %-24s %s ",opts[i], i==2?(online?"ONLINE":"OFFLINE"):"ON");
        txt(6,8+i*3,(i==item)?C_WHITE:C_SOFT,bg,b);
    }
    txt(6,22,C_DIM,BG,"Settings screen is UI-only in this build.");
}

static void render(void){
    cls(); draw_top_status(); draw_xmb_categories();
    if(section==0) draw_game();
    else if(section==1) draw_achievements();
    else if(section==2) draw_profile();
    else draw_settings();
    draw_popup();
    draw_footer(section==1?"L/R: filter achievements   TRIANGLE: network test":NULL);
}

static void evaluate(void){
    if(!unlocked && progress>=10){
        unlocked=1; popup_frames=240; dirty=1;
        log_line("UNLOCK achievement=1001 title=First_Contact");
        if(online){ synced=1; log_line("SYNC achievement=1001 status=confirmed"); }
        else { queued=1; log_line("QUEUE achievement=1001 state=pending"); }
    }
    if(online && queued && !synced){
        synced=1; queued=0; popup_frames=180; dirty=1;
        log_line("SYNC achievement=1001 status=confirmed_after_reconnect");
    }
}

static int section_item_count(void){
    if(section==1) return visible_count();
    if(section==3) return 4;
    return 1;
}

static void render_if_needed(void){
    if(!dirty) return;
    sceDisplayWaitVblankStart();
    render();
    dirty=0;
}

int main(int argc,char *argv[]){
    SceCtrlData pad,old; (void)argc;(void)argv;
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    memset(&old,0,sizeof(old));
    log_line("BOOT RA-PSP XMB UI v0.4");
    render_if_needed();

    while(1){
        unsigned int pressed; int count;
        sceCtrlReadBufferPositive(&pad,1);
        pressed=pad.Buttons & ~old.Buttons;

        if(pressed & PSP_CTRL_LEFT){ section=(section+3)%4; item=0; dirty=1; }
        if(pressed & PSP_CTRL_RIGHT){ section=(section+1)%4; item=0; dirty=1; }

        count=section_item_count();
        if(pressed & PSP_CTRL_UP){ if(count>0) item=(item+count-1)%count; dirty=1; }
        if(pressed & PSP_CTRL_DOWN){ if(count>0) item=(item+1)%count; dirty=1; }

        if(section==1){
            if(pressed & PSP_CTRL_LTRIGGER){ filter_mode=(filter_mode+2)%3; item=0; dirty=1; }
            if(pressed & PSP_CTRL_RTRIGGER){ filter_mode=(filter_mode+1)%3; item=0; dirty=1; }
        }

        if(pressed & PSP_CTRL_CROSS){
            if(section==0){ if(progress<10) progress++; evaluate(); dirty=1; }
            else if(section==1){ int idx=visible_count()?visible_to_real(item):0; char b[96]; snprintf(b,sizeof(b),"UI achievement_selected id=%d",idx); log_line(b); }
            else if(section==3 && item==2){ online=!online; evaluate(); dirty=1; }
        }

        if(pressed & PSP_CTRL_TRIANGLE){ online=!online; evaluate(); dirty=1; log_line(online?"NETWORK online":"NETWORK offline"); }
        if(pressed & PSP_CTRL_CIRCLE){ if(section!=0){ section=0; item=0; dirty=1; } else break; }

        if(popup_frames>0){ popup_frames--; if(popup_frames==0) dirty=1; }
        render_if_needed();
        old=pad;
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
