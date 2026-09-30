#include <pspkernel.h>
#include <pspthreadman.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

PSP_MODULE_INFO("RA-PSP-MEMTEST", PSP_MODULE_KERNEL, 0, 1);
PSP_MAIN_THREAD_ATTR(0);

static volatile int g_running = 1;
static const char* LOG_PATH = "ms0:/PSP/RA/logs/memory.log";

static void ensure_dirs(void) {
    sceIoMkdir("ms0:/PSP/RA", 0777);
    sceIoMkdir("ms0:/PSP/RA/logs", 0777);
}

static void append_log(const char* text) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_CREAT | PSP_O_WRONLY | PSP_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, (SceSize)strlen(text));
        sceIoClose(fd);
    }
}

static int memtest_thread(SceSize args, void* argp) {
    (void)args; (void)argp;
    ensure_dirs();
    append_log("RA-PSP MEMTEST v0.1 started\n");

    unsigned sample = 0;
    while (g_running) {
        char line[256];
        volatile uint32_t* base = (volatile uint32_t*)0x08800000;
        uint32_t v0 = 0, v1 = 0, v2 = 0, v3 = 0;

        /* Basic in-process reads. On real PSP/CFW the game plugin shares the game address space. */
        v0 = base[0];
        v1 = base[1];
        v2 = base[2];
        v3 = base[3];

        snprintf(line, sizeof(line),
                 "sample=%u addr=08800000 %08X %08X %08X %08X\n",
                 sample++, v0, v1, v2, v3);
        append_log(line);

        sceKernelDelayThread(1000000); /* 1 Hz to keep first test safe/light */
    }

    append_log("RA-PSP MEMTEST stopped\n");
    return 0;
}

int module_start(SceSize args, void* argp) {
    (void)args; (void)argp;
    SceUID thid = sceKernelCreateThread("ra_psp_memtest", memtest_thread, 0x18, 0x4000, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
    return 0;
}

int module_stop(SceSize args, void* argp) {
    (void)args; (void)argp;
    g_running = 0;
    return 0;
}
