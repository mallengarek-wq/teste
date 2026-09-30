#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psprtc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP PPSSPP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define W 480
#define H 272
#define HOLD_US 400000u
#define MONEY_ADDR ((volatile uint32_t*)0x0863AC2C)

static volatile int running=1, menu_open=0, popup_frames=0;
static uint64_t hold_start=0;
typedef struct{uint8_t r,g,b;} RGB;
static const RGB BG={12,24,40}, PANEL={31,52,74}, PANEL2={49,77,106}, ACC={132,207,255};
static const RGB WHITE={246,250,255}, SOFT={184,204,223}, DIM={105,127,148}, GREEN={149,232,185}, YELLOW={255,219,124};

static const unsigned char A[26][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
static const unsigned char D[10][7]={{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};

static unsigned char grow(char ch,int r){
 unsigned char c=(unsigned char)ch;if(c>='a'&&c<='z')c-=32;if(c>='A'&&c<='Z')return A[c-'A'][r];if(c>='0'&&c<='9')return D[c-'0'][r];
 switch(c){case ' ':return 0;case '.':return r==6?4:0;case ':':return (r==2||r==5)?4:0;case '-':return r==3?14:0;case '/':return r==0?1:r==1?2:r==2?2:r==3?4:r==4?8:r==5?8:16;case '+':return r==3?14:((r==2||r==4)?4:0);default:return (r==0||r==6)?14:((r==1||r==5)?17:0);}
}
static uint32_t c32(RGB c){return 0xFF000000u|((uint32_t)c.b<<16)|((uint32_t)c.g<<8)|c.r;}
static uint16_t c16(RGB c,int f){if(f==PSP_DISPLAY_PIXEL_FORMAT_565)return(uint16_t)(((c.r>>3)<<11)|((c.g>>2)<<5)|(c.b>>3));if(f==PSP_DISPLAY_PIXEL_FORMAT_5551)return(uint16_t)(0x8000|((c.r>>3)<<10)|((c.g>>3)<<5)|(c.b>>3));return(uint16_t)(0xF000|((c.b>>4)<<8)|((c.g>>4)<<4)|(c.r>>4));}
static void px(void*fb,int s,int f,int x,int y,RGB c){if(x<0||x>=W||y<0||y>=H)return;if(f==PSP_DISPLAY_PIXEL_FORMAT_8888)((uint32_t*)fb)[y*s+x]=c32(c);else((uint16_t*)fb)[y*s+x]=c16(c,f);}
static void rect(void*fb,int s,int f,int x0,int y0,int x1,int y1,RGB c){int x,y;if(x0<0)x0=0;if(y0<0)y0=0;if(x1>W)x1=W;if(y1>H)y1=H;for(y=y0;y<y1;y++)for(x=x0;x<x1;x++)px(fb,s,f,x,y,c);}
static void txt(void*fb,int s,int f,int x,int y,RGB c,const char*t,int sc){int i,r,k;if(!t)return;for(i=0;t[i]&&x<474;i++,x+=6*sc)for(r=0;r<7;r++){unsigned char b=grow(t[i],r);for(k=0;k<5;k++)if(b&(1<<(4-k))){int xx,yy;for(yy=0;yy<sc;yy++)for(xx=0;xx<sc;xx++)px(fb,s,f,x+k*sc+xx,y+r*sc+yy,c);}}}
static void box(void*fb,int s,int f,int x0,int y0,int x1,int y1,RGB b,RGB l){rect(fb,s,f,x0,y0,x1,y1,b);rect(fb,s,f,x0,y0,x1,y0+1,l);rect(fb,s,f,x0,y1-1,x1,y1,l);rect(fb,s,f,x0,y0,x0+1,y1,l);rect(fb,s,f,x1-1,y0,x1,y1,l);}
static int getfb(void**fb,int*s,int*f){if(sceDisplayGetFrameBuf(fb,s,f,PSP_DISPLAY_SETBUF_IMMEDIATE)<0||!*fb)return 0;return *f==PSP_DISPLAY_PIXEL_FORMAT_8888||*f==PSP_DISPLAY_PIXEL_FORMAT_565||*f==PSP_DISPLAY_PIXEL_FORMAT_5551||*f==PSP_DISPLAY_PIXEL_FORMAT_4444;}
static void clockdraw(void*fb,int s,int f){pspTime t;char b[16];if(sceRtcGetCurrentClockLocalTime(&t)>=0)snprintf(b,sizeof(b),"%02d:%02d",t.hour,t.minutes);else strcpy(b,"--:--");txt(fb,s,f,421,11,SOFT,b,1);}
static void menu(void){void*fb=0;int s=0,f=0;char b[64];uint32_t money;if(!getfb(&fb,&s,&f))return;money=*MONEY_ADDR;box(fb,s,f,18,18,462,254,BG,ACC);rect(fb,s,f,19,19,461,48,PANEL);txt(fb,s,f,30,27,WHITE,"RA-PSP",2);clockdraw(fb,s,f);txt(fb,s,f,30,59,WHITE,"PERSONA 2: INNOCENT SIN",1);txt(fb,s,f,30,73,SOFT,"ULUS10584  PPSSPP USER-MODE",1);txt(fb,s,f,350,59,GREEN,"PLUGIN LOADED",1);box(fb,s,f,30,96,440,126,PANEL2,ACC);txt(fb,s,f,42,105,WHITE,"LIVE GAME MEMORY",1);snprintf(b,sizeof(b),"MONEY 0x%08X",(unsigned)money);txt(fb,s,f,240,105,YELLOW,b,1);box(fb,s,f,30,137,440,177,PANEL,DIM);txt(fb,s,f,42,147,WHITE,"RETROACHIEVEMENTS",1);txt(fb,s,f,42,162,SOFT,"OVERLAY READY - ONLINE BRIDGE NEXT",1);box(fb,s,f,30,188,440,225,PANEL,DIM);txt(fb,s,f,42,198,WHITE,"TRIANGLE  TEST POPUP",1);txt(fb,s,f,42,212,SOFT,"CIRCLE  CLOSE MENU",1);txt(fb,s,f,30,239,DIM,"L+R+SELECT OPENS THIS MENU",1);}
static void popup(void){void*fb=0;int s=0,f=0;if(!getfb(&fb,&s,&f))return;box(fb,s,f,140,22,462,82,PANEL,ACC);rect(fb,s,f,151,33,190,71,PANEL2);txt(fb,s,f,162,47,ACC,"RA",1);txt(fb,s,f,202,34,WHITE,"ACHIEVEMENT POPUP TEST",1);txt(fb,s,f,202,50,SOFT,"OVERLAY RUNNING IN-GAME",1);txt(fb,s,f,202,66,GREEN,"PPSSPP USER-MODE OK",1);}
static int worker(SceSize a,void*p){SceCtrlData cur,prev;uint64_t now=0;(void)a;(void)p;memset(&prev,0,sizeof(prev));sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);while(running){sceCtrlPeekBufferPositive(&cur,1);if((cur.Buttons&(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT))==(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT)){sceRtcGetCurrentTick(&now);if(!hold_start)hold_start=now;else if(!menu_open&&now-hold_start>=HOLD_US){menu_open=1;hold_start=0;}}else hold_start=0;if(menu_open){if((cur.Buttons&PSP_CTRL_CIRCLE)&&!(prev.Buttons&PSP_CTRL_CIRCLE))menu_open=0;if((cur.Buttons&PSP_CTRL_TRIANGLE)&&!(prev.Buttons&PSP_CTRL_TRIANGLE))popup_frames=180;}sceDisplayWaitVblankStart();if(menu_open)menu();if(popup_frames>0){popup();popup_frames--;}prev=cur;}sceKernelExitDeleteThread(0);return 0;}
int module_start(SceSize a,void*p){SceUID th;(void)a;(void)p;th=sceKernelCreateThread("RA-PSP PPSSPP",worker,0x18,0x4000,PSP_THREAD_ATTR_USER,0);if(th>=0)sceKernelStartThread(th,0,0);return 0;}
int module_stop(SceSize a,void*p){(void)a;(void)p;running=0;return 0;}
