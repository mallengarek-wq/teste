#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ra_memory_psp.h"

#define RA_PSP_RAM_BASE 0x08000000u
#define RA_PSP_RAM_SIZE 0x04000000u

uint32_t ra_psp_read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, struct rc_client_t* client) {
    uint64_t end = (uint64_t)address + (uint64_t)num_bytes;
    const void* src;
    (void)client;

    if (!buffer || num_bytes == 0 || end > RA_PSP_RAM_SIZE)
        return 0;

    src = (const void*)(uintptr_t)(RA_PSP_RAM_BASE + address);
    memcpy(buffer, src, num_bytes);
    return num_bytes;
}
