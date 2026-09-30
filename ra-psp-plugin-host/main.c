#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <stdio.h>

PSP_MODULE_INFO("RA-PSP Plugin Host", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define HOST_LOG "ms0:/PSP/PLUGINS/RA-PSP/host_test.log"
#define PLUGIN_PATH "ms0:/PSP/PLUGINS/RA-PSP/ra_psp_ppsspp.prx"

static void log_line(const char *msg) {
    SceUID fd = sceIoOpen(HOST_LOG, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        int n = 0;
        while (msg[n]) n++;
        sceIoWrite(fd, msg, n);
        sceIoWrite(fd, "\r\n", 2);
        sceIoClose(fd);
    }
    pspDebugScreenPrintf("%s\n", msg);
}

int main(int argc, char *argv[]) {
    SceUID mod;
    int status = 0;
    int ret;
    char line[128];
    (void)argc;
    (void)argv;

    pspDebugScreenInit();
    log_line("HOST: start");
    log_line("HOST: loading RA-PSP PRX");

    mod = sceKernelLoadModule(PLUGIN_PATH, 0, NULL);
    snprintf(line, sizeof(line), "HOST: sceKernelLoadModule = 0x%08X", (unsigned int)mod);
    log_line(line);

    if (mod >= 0) {
        ret = sceKernelStartModule(mod, 0, NULL, &status, NULL);
        snprintf(line, sizeof(line), "HOST: sceKernelStartModule = 0x%08X status=0x%08X", (unsigned int)ret, (unsigned int)status);
        log_line(line);
    } else {
        log_line("HOST: load failed");
    }

    log_line("HOST: waiting 10 seconds");
    sceKernelDelayThread(10000000);
    log_line("HOST: done");
    sceKernelExitGame();
    return 0;
}
