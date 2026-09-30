#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psprtc.h>
#include <pspiofilemgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_client.h"
#include "ra_rc_bridge.h"

PSP_MODULE_INFO("RA-PSP", PSP_MODULE_KERNEL, 0, 9);
PSP_MAIN_THREAD_ATTR(0);

#define CONFIG_PATH "ms0:/SEPLUGINS/RA-PSP/auth.ini"
#define LOG_DIR     "ms0:/SEPLUGINS/RA-PSP/logs"
#define LOG_PATH    "ms0:/SEPLUGINS/RA-PSP/logs/plugin.log"
#define MENU_HOLD_US 400000u
#define POPUP_FRAMES 240
#define MAX_TEXT 96

static volatile int g_running = 1;
static volatile int g_login_ok = 0;
static volatile int g_load_requested = 0;
static volatile int g_game_loaded = 0;
static volatile int g_online = 0;
static volatile int g_menu_open = 0;
static volatile int g_popup_frames = 0;
static volatile int g_filter = 0; /* 0 all, 1 unlocked, 2 locked */
static volatile int g_menu_index = 0;

static char g_username[64];
static char g_token[128];
static char g_hash[40];
static char g_popup_title[96];
static char g_popup_desc[160];
static unsigned g_popup_points = 0;

typedef struct { uint8_t r,g,b; } RGB;
static const RGB C_BG = {14, 27, 43};
static const RGB C_PANEL = {38, 58, 78};
static const RGB C_PANEL2 = {48, 73, 98};
static const RGB C_ACCENT = {130, 205, 255};
static const RGB C_WHITE = {245, 249, 255};
static const RGB C_SOFT = {190, 207, 222};
static const RGB C_DIM = {115, 135, 153};
static const RGB C_GREEN = {155, 232, 188};
static const RGB C_YELLOW = {255, 218, 122};

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

static void ensure_dirs(void) {
    sceIoMkdir("ms0:/SEPLUGINS/RA-PSP", 0777);
    sceIoMkdir(LOG_DIR, 0777);
}

static void log_line(const char* s) {
    SceUID fd;
    if (!s) return;
    fd = sceIoOpen(LOG_PATH, PSP_O_CREAT | PSP_O_WRONLY | PSP_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, s, (SceSize)strlen(s));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void trim(char* s) {
    char* p;
    size_t n;
    if (!s) return;
    while (*s==' ' || *s=='\t') memmove(s,s+1,strlen(s));
    n=strlen(s); while(n && (s[n-1]=='\r'||s[n-1]=='\n'||s[n-1]==' '||s[n-1]=='\t')) s[--n]=0;
    p=strchr(s,'#'); if(p) *p=0;
}

static int load_config(void) {
    SceUID fd;
    char buf[1024];
    int n;
    char* line;
    memset(g_username,0,sizeof(g_username));
    memset(g_token,0,sizeof(g_token));
    memset(g_hash,0,sizeof(g_hash));
    fd=sceIoOpen(CONFIG_PATH,PSP_O_RDONLY,0);
    if(fd<0) return -1;
    n=sceIoRead(fd,buf,sizeof(buf)-1); sceIoClose(fd);
    if(n<=0) return -2; buf[n]=0;
    line=strtok(buf,"\n");
    while(line){
        char* eq=strchr(line,'=');
        if(eq){ *eq=0; trim(line); trim(eq+1);
            if(strcmp(line,"username")==0) snprintf(g_username,sizeof(g_username),"%s",eq+1);
            else if(strcmp(line,"token")==0) snprintf(g_token,sizeof(g_token),"%s",eq+1);
            else if(strcmp(line,"game_hash")==0) snprintf(g_hash,sizeof(g_hash),"%s",eq+1);
        }
        line=strtok(NULL,"\n");
    }
    return (g_username[0]&&g_token[0]&&strlen(g_hash)==32)?0:-3;
}

static unsigned char glyph_row(char ch,int row) {
    unsigned char c=(unsigned char)ch;
    if(c>='a'&&c<='z') c=(unsigned char)(c-'a'+'A');
    if(c>='A'&&c<='Z') return font_alpha[c-'A'][row];
    if(c>='0'&&c<='9') return font_digit[c-'0'][row];
    switch(c){
        case ' ': return 0; case '.': return row==6?4:0; case ':': return (row==2||row==5)?4:0;
        case '-': return row==3?14:0; case '/': return row==0?1:row==1?2:row==2?2:row==3?4:row==4?8:row==5?8:16;
        case '+': return row==3?14:((row==2||row==4)?4:0); case '%': return row==0?17:row==1?2:row==2?4:row==3?4:row==4?8:row==5?16:17;
        case '[': return (row==0||row==6)?14:8; case ']': return (row==0||row==6)?14:2;
        case '!': return row<5?4:(row==6?4:0); case '?': return row==0?14:row==1?17:row==2?2:row==3?4:row==5?4:0;
        default: return row==0||row==6?14:((row==1||row==5)?17:0);
    }
}

static uint32_t abgr(RGB c){ return 0xFF000000u | ((uint32_t)c.b<<16) | ((uint32_t)c.g<<8) | c.r; }
static uint16_t pack16(RGB c,int fmt){
    if(fmt==PSP_DISPLAY_PIXEL_FORMAT_565) return (uint16_t)(((c.r>>3)<<11)|((c.g>>2)<<5)|(c.b>>3));
    if(fmt==PSP_DISPLAY_PIXEL_FORMAT_5551) return (uint16_t)(0x8000|((c.r>>3)<<10)|((c.g>>3)<<5)|(c.b>>3));
    return (uint16_t)(0xF000|((c.b>>4)<<8)|((c.g>>4)<<4)|(c.r>>4));
}

static void putpx(void* fb,int stride,int fmt,int x,int y,RGB c){
    if(x<0||x>=480||y<0||y>=272) return;
    if(fmt==PSP_DISPLAY_PIXEL_FORMAT_8888) ((uint32_t*)fb)[y*stride+x]=abgr(c);
    else ((uint16_t*)fb)[y*stride+x]=pack16(c,fmt);
}

static void fill(void* fb,int stride,int fmt,int x0,int y0,int x1,int y1,RGB c){
    int x,y; if(x0<0)x0=0;if(y0<0)y0=0;if(x1>480)x1=480;if(y1>272)y1=272;
    for(y=y0;y<y1;y++) for(x=x0;x<x1;x++) putpx(fb,stride,fmt,x,y,c);
}

static void text(void* fb,int stride,int fmt,int x,int y,RGB c,const char* s,int scale){
    int i,row,col; if(!s) return;
    for(i=0;s[i]&&x<475;i++,x+=6*scale){
        unsigned char bits;
        for(row=0;row<7;row++){
            bits=glyph_row(s[i],row);
            for(col=0;col<5;col++) if(bits&(1<<(4-col))){
                int xx,yy; for(yy=0;yy<scale;yy++) for(xx=0;xx<scale;xx++) putpx(fb,stride,fmt,x+col*scale+xx,y+row*scale+yy,c);
            }
        }
    }
}

static void box(void* fb,int stride,int fmt,int x0,int y0,int x1,int y1,RGB fillc,RGB line){
    fill(fb,stride,fmt,x0,y0,x1,y1,fillc);
    fill(fb,stride,fmt,x0,y0,x1,y0+1,line); fill(fb,stride,fmt,x0,y1-1,x1,y1,line);
    fill(fb,stride,fmt,x0,y0,x0+1,y1,line); fill(fb,stride,fmt,x1-1,y0,x1,y1,line);
}

static int achievement_matches(const rc_client_achievement_t* a){
    int u=a && a->unlocked!=RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
    return a && (g_filter==0 || (g_filter==1&&u) || (g_filter==2&&!u));
}

static int collect_achievements(const rc_client_achievement_t** out,int max,int* total_match){
    rc_client_t* c=ra_rc_client();
    rc_client_achievement_list_t* list;
    int n=0,total=0,b,i;
    if(!c||!rc_client_has_achievements(c)){ if(total_match)*total_match=0; return 0; }
    list=rc_client_create_achievement_list(c,RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if(!list){ if(total_match)*total_match=0; return 0; }
    for(b=0;b<(int)list->num_buckets;b++) for(i=0;i<(int)list->buckets[b].num_achievements;i++){
        const rc_client_achievement_t* a=list->buckets[b].achievements[i];
        if(!achievement_matches(a)) continue;
        if(total>=g_menu_index && n<max) out[n++]=a;
        total++;
    }
    rc_client_destroy_achievement_list(list);
    if(total_match)*total_match=total;
    return n;
}

static void draw_clock(void* fb,int stride,int fmt){
    pspTime t; char b[32];
    if(sceRtcGetCurrentClockLocalTime(&t)>=0) snprintf(b,sizeof(b),"%02d/%02d %02d:%02d",t.month,t.day,t.hour,t.minutes);
    else snprintf(b,sizeof(b),"--/-- --:--");
    text(fb,stride,fmt,382,9,C_SOFT,b,1);
}

static void draw_menu(void){
    void* fb=NULL; int stride=0,fmt=0;
    const rc_client_achievement_t* rows[7];
    rc_client_t* client=ra_rc_client();
    const rc_client_game_t* game=client?rc_client_get_game_info(client):NULL;
    const rc_client_user_t* user=client?rc_client_get_user_info(client):NULL;
    rc_client_user_game_summary_t summary;
    int count,total,i;
    char b[128];
    if(sceDisplayGetFrameBuf(&fb,&stride,&fmt,PSP_DISPLAY_SETBUF_IMMEDIATE)<0||!fb) return;
    if(fmt!=PSP_DISPLAY_PIXEL_FORMAT_8888&&fmt!=PSP_DISPLAY_PIXEL_FORMAT_565&&fmt!=PSP_DISPLAY_PIXEL_FORMAT_5551&&fmt!=PSP_DISPLAY_PIXEL_FORMAT_4444) return;

    box(fb,stride,fmt,18,18,462,254,C_BG,C_ACCENT);
    fill(fb,stride,fmt,19,19,461,44,C_PANEL);
    text(fb,stride,fmt,30,27,C_WHITE,"RA-PSP",2);
    draw_clock(fb,stride,fmt);
    text(fb,stride,fmt,30,55,C_SOFT,game&&game->title?game->title:"NO RETROACHIEVEMENTS GAME LOADED",1);

    memset(&summary,0,sizeof(summary));
    if(client&&game) rc_client_get_user_game_summary(client,&summary);
    if(game){
        snprintf(b,sizeof(b),"%u / %u UNLOCKED   %u / %u PTS",summary.num_unlocked_achievements,summary.num_core_achievements,summary.points_unlocked,summary.points_core);
        text(fb,stride,fmt,30,69,C_SOFT,b,1);
    } else {
        text(fb,stride,fmt,30,69,C_YELLOW,"LOGIN/HASH REQUIRED - CHECK AUTH.INI",1);
    }
    snprintf(b,sizeof(b),"USER: %s    %s",user&&user->display_name?user->display_name:"NOT SIGNED IN",g_online?"ONLINE":"OFFLINE");
    text(fb,stride,fmt,30,82,g_online?C_GREEN:C_DIM,b,1);

    box(fb,stride,fmt,30,99,160,118,g_filter==0?C_PANEL2:C_PANEL,C_ACCENT);
    box(fb,stride,fmt,160,99,300,118,g_filter==1?C_PANEL2:C_PANEL,C_ACCENT);
    box(fb,stride,fmt,300,99,440,118,g_filter==2?C_PANEL2:C_PANEL,C_ACCENT);
    text(fb,stride,fmt,79,105,g_filter==0?C_WHITE:C_DIM,"ALL",1);
    text(fb,stride,fmt,197,105,g_filter==1?C_WHITE:C_DIM,"UNLOCKED",1);
    text(fb,stride,fmt,349,105,g_filter==2?C_WHITE:C_DIM,"LOCKED",1);

    count=collect_achievements(rows,7,&total);
    if(g_menu_index>=total && total>0) g_menu_index=total-1;
    for(i=0;i<count;i++){
        int y=125+i*16; int unlocked=rows[i]->unlocked!=RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
        RGB bg=(i==0)?C_PANEL2:C_PANEL;
        box(fb,stride,fmt,30,y,440,y+14,bg,(i==0)?C_ACCENT:C_DIM);
        snprintf(b,sizeof(b),"%.45s",rows[i]->title?rows[i]->title:"ACHIEVEMENT");
        text(fb,stride,fmt,37,y+3,unlocked?C_WHITE:(i==0?C_WHITE:C_SOFT),b,1);
        snprintf(b,sizeof(b),"%u",rows[i]->points); text(fb,stride,fmt,412,y+3,C_SOFT,b,1);
    }
    if(count==0) text(fb,stride,fmt,45,145,C_DIM,game?"NO ACHIEVEMENTS IN THIS FILTER":"WAITING FOR REAL RC_CLIENT DATA",1);
    text(fb,stride,fmt,31,239,C_SOFT,"UP/DOWN SELECT   TRIANGLE FILTER   O CLOSE",1);
    sceKernelDcacheWritebackAll();
}

static void draw_popup(void){
    void* fb=NULL; int stride=0,fmt=0; char b[128];
    if(g_popup_frames<=0) return;
    if(sceDisplayGetFrameBuf(&fb,&stride,&fmt,PSP_DISPLAY_SETBUF_IMMEDIATE)<0||!fb) return;
    box(fb,stride,fmt,180,18,468,78,C_BG,C_ACCENT);
    text(fb,stride,fmt,195,28,C_GREEN,"ACHIEVEMENT UNLOCKED",1);
    snprintf(b,sizeof(b),"%.38s",g_popup_title); text(fb,stride,fmt,195,42,C_WHITE,b,1);
    snprintf(b,sizeof(b),"%.45s",g_popup_desc); text(fb,stride,fmt,195,54,C_SOFT,b,1);
    snprintf(b,sizeof(b),"+%u PTS",g_popup_points); text(fb,stride,fmt,400,66,C_YELLOW,b,1);
    sceKernelDcacheWritebackAll();
}

static void on_ui_event(const ra_ui_event_t* e,void* userdata){
    (void)userdata;
    if(!e) return;
    if(e->type==RA_UI_EVENT_LOGIN_OK){ g_login_ok=1; g_online=1; g_load_requested=1; log_line("login ok"); }
    else if(e->type==RA_UI_EVENT_LOGIN_FAILED){ g_online=0; log_line("login failed"); }
    else if(e->type==RA_UI_EVENT_GAME_LOADED){ g_game_loaded=1; log_line("game loaded"); }
    else if(e->type==RA_UI_EVENT_GAME_LOAD_FAILED){ g_game_loaded=0; log_line("game load failed"); }
    else if(e->type==RA_UI_EVENT_SERVER_OFFLINE){ g_online=0; }
    else if(e->type==RA_UI_EVENT_SERVER_ONLINE){ g_online=1; }
    else if(e->type==RA_UI_EVENT_ACHIEVEMENT_UNLOCKED){
        snprintf(g_popup_title,sizeof(g_popup_title),"%s",e->title);
        snprintf(g_popup_desc,sizeof(g_popup_desc),"%s",e->description);
        g_popup_points=e->points; g_popup_frames=POPUP_FRAMES;
        log_line("achievement triggered");
    }
}

static int runtime_thread(SceSize args,void* argp){
    SceCtrlData pad,old; uint32_t hold_start=0; int hot_latched=0; int login_started=0;
    (void)args;(void)argp; memset(&old,0,sizeof(old));
    ensure_dirs(); log_line("RA-PSP 0.9 in-game started");
    ra_rc_init(on_ui_event,NULL);
    if(load_config()==0){ login_started=1; ra_rc_login_with_token(g_username,g_token); log_line("login requested"); }
    else log_line("auth.ini missing or incomplete");

    while(g_running){
        unsigned pressed;
        sceCtrlPeekBufferPositive(&pad,1); pressed=pad.Buttons & ~old.Buttons;
        if((pad.Buttons&PSP_CTRL_LTRIGGER)&&(pad.Buttons&PSP_CTRL_RTRIGGER)&&(pad.Buttons&PSP_CTRL_SELECT)){
            uint32_t now=sceKernelGetSystemTimeLow();
            if(!hold_start) hold_start=now;
            if(!hot_latched && (uint32_t)(now-hold_start)>=MENU_HOLD_US){ g_menu_open=!g_menu_open; hot_latched=1; }
        } else { hold_start=0; hot_latched=0; }

        if(g_menu_open){
            if(pressed&PSP_CTRL_CIRCLE) g_menu_open=0;
            if(pressed&PSP_CTRL_TRIANGLE){ g_filter=(g_filter+1)%3; g_menu_index=0; }
            if(pressed&PSP_CTRL_UP){ if(g_menu_index>0) g_menu_index--; }
            if(pressed&PSP_CTRL_DOWN) g_menu_index++;
            draw_menu();
        } else if(g_popup_frames>0){ draw_popup(); g_popup_frames--; }

        if(g_load_requested&&g_login_ok&&g_hash[0]){ g_load_requested=0; ra_rc_load_game_hash(g_hash); log_line("game hash load requested"); }
        if(login_started) ra_rc_do_frame();
        old=pad;
        sceKernelDelayThread(16666);
    }
    ra_rc_shutdown(); log_line("RA-PSP stopped"); return 0;
}

int module_start(SceSize args,void* argp){
    SceUID th; (void)args;(void)argp;
    g_running=1;
    th=sceKernelCreateThread("ra_psp_runtime",runtime_thread,0x18,0x9000,0,NULL);
    if(th>=0) sceKernelStartThread(th,0,NULL);
    return 0;
}

int module_stop(SceSize args,void* argp){ (void)args;(void)argp; g_running=0; return 0; }
